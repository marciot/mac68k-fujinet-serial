mac68k-fujinet-serial
=====================

This repository contains a set of experimental Macintosh drivers for the [FujiNet project]. The purpose
of these drivers is to demonstrate a method of transmitting non-disk data through the floppy port.

The software consists of a Macintosh Desk Accessory that allows the user to create a virtual modem or
printer port. It can also install a virtual network driver, but this is just a non-functional stub.

The virtual drivers then talk to the ESP32 residing on the [FujiNet adapter] via the floppy port,
piggy-backing on ordinary DCD block I/O. When used with a specially modified version of FujiNet,
the ESP32 will monitor read and write requests and treat access to certain blocks as a serial I/O.

Presently, the ESP32 can operate in one of two modes:

1. It can act as a loopback interface, echoing back any received data
2. Bridge the connection to an external host via the USB port

[![FujiNet Serial Demonstration](https://github.com/marciot/mac68k-fujinet/raw/main/images/youtube.png)](https://youtu.be/d1GNirCGzVg)

:tv: See a demo on [YouTube]!

[YouTube]: https://youtu.be/d1GNirCGzVg

How To Install:
---------------

To use this project, it is necessary to use the "fujinet-floppy-serial-bridge" branch of my [FujiNet fork].

Then, set either "MAC_SERIAL_LOOPBACK_TEST" or "MAC_SERIAL_USB_SERIAL_TEST" in [floppy_serial_handler.cpp] to 1,
but not both, to configure the operating mode.

Once the modified firmware has been flashed, boot from either one of the disk images from the
[latest release](../../releases/latest) as a DCD volume using FujiNet. Then, open the "FujiNet" Desk Accessory.
It will attempt to connect with Pico. Once "FujiNet Status" changes to "Connected", check either the "Modem Port"
or "Printer Port" to redirect that port.

**Selecting more than one at a time, or using the "MacTCP" option is not currently supported.**

How It Works:
-------------

The Macintosh uses different drivers for communications. These are as follows:

* .AIn: Modem port input
* .AOut: Modem port output
* .BIn: Printer port input
* .BOut: Printer port output
* .IPP: MacTCP networking

The code for each driver must be preset either in ROM or in RAM and then a DCE
(device control entry) is added to the OS device unit table to make it usable
by applications. In addition, desk accessories are drivers too.

The Apple drivers are written in assembly language and with the exception of .IPP,
all their code stored in ROM. I wanted to write as much of my driver code in C as
possible, while still making it small enough to fit on a 512 KB Mac or 128 KB Mac.

I wanted to made one driver, called [.Fuji], and have it handle the functions
for all the native drivers I was going to patch. The name Mac OS uses to find
a driver is embedded in a header preceeding the code, so it is not possible
to have differently named drivers share the same code. To get around this, I created
a small [stub] driver in assembly languge that serves as a placeholder for the name,
but immediatly passes control to the main [.Fuji] driver. The stub driver, which
is loaded into memory twice for each port, is only a few bytes, while the [.Fuji]
driver is quite a bit larger.

**NOTE: There are actually two versions of the [.Fuji] driver. "FujiSerialAsync.c"
is the preferred one, but there is also a "FujiSerialBasic.c" which is an earlier
attempt. It does not support asynchronous I/O, which is a major feature of Mac OS
drivers.**


![Driver Architecture][architecture]

The [Desk Accessory] presents the UI to the user, but the job of loading
the drivers into memory is handled by "FujiSerialInit.c" in [FujiCommon].
The file "FujiFloppyInit.c" handles the block I/O operations that establish
a connection to the Pico. "LedIndicators.h" is a small library for drawing
the status icons on the menu bar. It is written in assembly to be very, very
small and fast.

These files currently live in the [FujiCommon] directory because there is also
a Macintosh test app in [FujiTests] that can be used for testing. It is capable
of loading the drivers, independently of the Desk Accessory, while printing
troubleshooting messages.

There is a [linux] directory with tools that can be run on a Linux host
for testing when connected to the Pico via USB. It demonstrates how to
make a loopback device.

[floppy_serial_handler.cpp]: https://github.com/marciot/fujinet-firmware/blob/6e289701f7c8e8d0bf71c6c6ffdf7b793a6349fb/lib/device/mac/floppy_serial_handler.cpp#L20
[FujiNet project]: https://fujinet.online
[FujiNet adapter]: https://github.com/djtersteegc/Apple-68k-FujiNet
[FujiNet fork]: https://github.com/marciot/fujinet-firmware
[demonstration]: https://www.youtube.com/watch?v=d1GNirCGzVg
[architecture]: https://github.com/marciot/mac68k-fujinet/raw/main/images/driver_diagram.svg "FujiNet Architecture"
[linux]: linux/
[FujiTests]: FujiTests
[FujiCommon]: FujiCommon/
[Desk Accessory]: FujiDeskAcc/FujiDeskAcc.c
[stub]: FujiSerial/FujiSerialStub.c
[.Fuji]: FujiSerial/FujiSerialAsync.c
