# Digital Equipment LK201 keyboard emulator

This project allows a standard USB PC keyboard to be used as replacement for a DEC LK201 keyboard.  It consists of a small circuit built around an Adafruit Trinket M0 Arduino processor, and software for that to implement a USB host driver for the USB keyboard and the LK201 communication protocol. 

Subdirectory `lk201` is the Arduino software.  You can build it using the standard Arduino SDK.  (The prebuilt `lk201.uf2` predates the 2026 protocol fixes; rebuild it.)

Subdirectory `lk201_rp2040` is a port to RP2040/RP2350 boards with a USB-A host port (Adafruit Feather RP2040 USB Host, Waveshare RP2350-USB-A, or a Pico).  See its README.

Subdirectory `common` holds `lk201_core.h`, the hardware independent LK201 protocol code used by both sketches, and `test` holds PC-side unit tests for it (`make -C test`).

Subdirectory `kicad` is the circuit board design.  You can view it using the open source KiCAD package.

Subdirectory `doc` contains several documentation files in MD format.  File `emulator.md` describes the features of the emulator; `kicad` describes the board design and assembly information, and `software` describes the software and the load procedure.  There are also two JPG files that show the circuit board.

File `LICENSE` is the open source license under which this emulator is released.

