#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <linux/uinput.h>
#include <termios.h>
#include <stdint.h>

#define msleep(ms) usleep((ms)*1000)

static void setup_abs(int fd, unsigned chan, int min, int max);

const char *progname;

static void usage_exit()
{
  fprintf(stderr, "usage: %s [-s N] <serial-device>\n", progname);
  fprintf(stderr, "       -s N    print out a receive packet once every N packets\n");
  fprintf(stderr, "               (defaults to 0: don't print any packets).\n");
  fprintf(stderr, "       -t      test mode: parse rx serial stream but don't create\n");
  fprintf(stderr, "               joystick device).\n");
  exit(2);
}


int main(int argc, char *argv[])
{
  int c;
  int sampleperiod = 0;
  int testmode = 0;

  progname = argv[0];
  
  while ((c = getopt (argc, argv, "ts:")) != -1)
    switch (c)
      {
      case 's':
        sampleperiod = atoi(optarg);
        break;
      case 't':
        testmode = 1;
        break;
      default:
        usage_exit();
      }

  argc -= optind;
  argv += optind;

  if (argc != 1)
    usage_exit();
  
  int sfd = open(argv[0], O_RDONLY |  O_NOCTTY | O_NDELAY);

  if (sfd < 0)
    {
      fprintf(stderr,"eror opening '%s': %s\n", argv[0], strerror(errno));
      return 1;
    }

  // configure serial port
  
  struct termios t;
  tcgetattr(sfd, &t);
  cfmakeraw(&t);
  t.c_cflag = CLOCAL+CREAD+CS8;
  t.c_oflag = 0;
  t.c_iflag = 0;
  cfsetispeed(&t,B9600);
  cfsetospeed(&t,B9600);
  tcsetattr(sfd,TCSANOW,&t);

  tcflush(sfd, TCIOFLUSH);
  msleep(0.1);
  tcflush(sfd, TCIOFLUSH);
  msleep(0.1);

  fcntl(sfd, F_SETFL, 0);
  
  // read FMSPIC stream to find number of channels

  int  numchan = 0;
  uint8_t syncbyte = 0;
  uint8_t b;

  for (int i=0; i<32; ++i)
    {
      if (1 != read(sfd, &b, 1))
        {
          perror("error reading serial port");
          return 1;
        }
      if ((b & 0xf0) == 0xf0)
        numchan = (b & 0x0f)-1;
    }

  printf("%d channels\n", numchan);

  if (numchan == 0)
    return 1;

  syncbyte = 0xf0 + numchan + 1;

  int ufd;
  
  if (!testmode)
    {
      ufd = open("/dev/uinput", O_WRONLY | O_NONBLOCK);
  
      if (ufd < 0)
        {
          perror("open /dev/uinput");
          return 1;
        }

      ioctl(ufd, UI_SET_EVBIT, EV_ABS); // enable analog absolute position handling

      // we assume that ABS_... channel ids start at 0
  
      for (int i=0; i<numchan; ++i)
        setup_abs(ufd, i,  0, 0xfe);
  
      struct uinput_setup setup =
        {
         .name = "FMSPIC",
         .id =
         {
          .bustype = BUS_USB,
          .vendor  = 0x3,
          .product = 0x3,
          .version = 2,
         }
        };

      if (ioctl(ufd, UI_DEV_SETUP, &setup))
        {
          perror("UI_DEV_SETUP");
          return 1;
        }
  
      if (ioctl(ufd, UI_DEV_CREATE))
        {
          perror("UI_DEV_CREATE");
          return 1;
        }
    }
  
  int pktcount = 0;
  static uint8_t rxbuf[128];
  int rxcnt = 0;

  while(1)
    {
      int n = read(sfd, rxbuf+rxcnt, (sizeof rxbuf)-rxcnt);

      if (n < 0)
        {
          perror("error reading serial port");
          return 1;
        }

      // seach back from last byte to find start of a complete packet

      rxcnt += n;

      int j;

      for (j = rxcnt-(numchan+1); j >= 0; --j)
        if (rxbuf[j] == syncbyte)
          break;

      // read more if we don't have a complete packet
      
      if (j < 0)
        continue;  
      
      // most recent complete packet's sync byte is at offset j, so
      // point to first data byte we're going to process

      uint8_t *p = rxbuf+j+1;

      ++pktcount;
      
      if (sampleperiod && (pktcount % sampleperiod) == 0)
        {
          // dump packet (including preceding sync byte)
          printf("%6d: ",pktcount);
          for (int i = -1; i < numchan; ++i)
            printf(" %02x",p[i]);
          printf("\n");
        }


      // send data to Linux input subsystem
      if (!testmode)
        {
          struct input_event ev[numchan+1];
          memset(&ev, 0, sizeof ev);
      
          for (int i=0; i<numchan; ++i)
            {
              ev[i].type = EV_ABS;
              ev[i].code = i;
              ev[i].value = p[i];
            }

          // sync event tells input layer we're done with a "batch" of
          // updates
    
          ev[numchan].type = EV_SYN;
          ev[numchan].code = SYN_REPORT;
          ev[numchan].value = 0;

          if(write(ufd, &ev, sizeof ev) < 0)
            {
              perror("write");
              return 1;
            }
        }
      
      // how much leftover data do we have in buffer after packet we
      // just handled?
      rxcnt -= (p+numchan)-rxbuf;
      memmove(rxbuf, p+numchan, rxcnt);
      
      // don't hog CPU when something's wrong
      msleep(1);
    }

  // we never get here, but if we someday exit the while loop, here's
  // how to clean up nicely
  
  if (!testmode)
    {
      if(ioctl(ufd, UI_DEV_DESTROY))
        {
          printf("UI_DEV_DESTROY");
          return 1;
        }
      close(ufd);
    }
  
  return 0;
}


// enable and configure an absolution "position" analog channel

__attribute__((unused))
static void setup_abs(int fd, unsigned chan, int min, int max)
{
  if (ioctl(fd, UI_SET_ABSBIT, chan))
    perror("UI_SET_ABSBIT");
  
  struct uinput_abs_setup s =
    {
     .code = chan,
     .absinfo = { .minimum = min,  .maximum = max },
    };

  if (ioctl(fd, UI_ABS_SETUP, &s))
    perror("UI_ABS_SETUP");
}


