## fmspic-uinput: a uerspace joystick driver for FMSPIC RC Transmitter Adapter

This is a userspace joystick driver for an RC transmitter connected to a serial port via a 9600 baud "FMS PIC" adapter cable.

It uses the [uinput API](https://www.kernel.org/doc/html/v4.12/input/uinput.html) to create a joystick device instance and feed input data to that device.  The input is read from the serial port to which the FMSPIC adapter cable is attached.

I use it with both the crrcsim RC airplane flight simulator, and the Heli-X RC helicopter and airplane flight simulator. It provides a standard joystick device and should be usable with any application that uses the normal joystick API.

Note: there are two mutually-incompatible types of "FMS PIC" serial cables:

 1. Sync byte of 0XF0+(#channels+1) running at 9600 baud.

 2. Sync byte of 0xFF running at 19200 baud.

This driver only supports the 9600 baud 0xF0+#channels+1 variety. It shouldn't be hard to add support for the other variety, but I don't have one to test with.
