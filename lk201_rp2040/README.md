# LK201 emulator — RP2040 / RP2350 version

This port runs the same LK201 protocol code as the Trinket M0 version
(`lk201_core.h`). It replaces the SAMD21 USB host library with TinyUSB
running on a PIO-based USB host port. The board's own USB-C port stays
free, so you can reprogram the board (hold BOOT, then drag a `.uf2`) and
watch a live debug log without unplugging anything.

## Boards

| Board | USB-A host pins | Notes |
| --- | --- | --- |
| Adafruit Feather RP2040 with USB Type A Host (#5723) | D+ 16 / D- 17 | Easiest choice. It has a USB-A jack and a 5 V boost with a 500 mA fuse (enabled on GPIO 18; the sketch handles this), and the red LED on GPIO 13 serves as the WAIT LED. |
| Waveshare RP2350-USB-A | D+ 12 / D- 13 | About $6, with castellated pads. Its USB-A 5 V comes straight from the 5 V pin. There is no plain LED, so wire one to a GPIO and set `WAIT_LED`. |
| Raspberry Pi Pico / Pico 2 + USB-A breakout | any GPIO pair, D- = D+ + 1 | Add 22 Ω series resistors on D+/D-, and feed VBUS from your 5 V supply. |

## Building

1. Install the "Raspberry Pi Pico/RP2040/RP2350" boards package (Earle
   Philhower) in Arduino.
2. Install the "Adafruit TinyUSB Library" and "Pico PIO USB" libraries.
3. Under Tools, choose your board, set **CPU Speed = 120 MHz** (PIO-USB
   needs a multiple of 12 MHz), and set **USB Stack = Adafruit
   TinyUSB**.
4. Check the pin defines at the top of `lk201_rp2040.ino`
   (`USB_HOST_DP`, `LK_TX_PIN`, `LK_RX_PIN`, `BEEP_PIN`, `WAIT_LED`).
5. Build and upload, or use Sketch > Export Compiled Binary and copy the
   `.uf2` onto the board's boot drive.

If the CPU clock is wrong, the WAIT LED blinks fast and the USB host
port stays off.

## Wiring

* **Serial link:** use the same level shifter as the Trinket board
  (ICL3232 / MAX3232 in a 16-pin DIP, running on 3.3 V). Wire T1IN to
  `LK_TX_PIN` and R1OUT to `LK_RX_PIN`. The RJ-22 side is identical to
  the existing KiCad schematic. The link runs at 4800 baud, 8N1.
* **Power:** the DEC host supplies +12 V on the keyboard cable. Use a
  12 V → 5 V **buck** module rated at 1 A or more (MP1584, Pololu D24V10F5
  or similar), because a linear regulator gets hot with a backlit
  keyboard attached. Feed 5 V to the Feather's `USB` pin or to the
  Waveshare's `5V` pin. A weak supply is a common cause of USB
  enumeration loops, which show up as blinking LEDs.
* **Beeper:** `BEEP_PIN` puts out a 2 kHz PWM signal, and the volume is
  set by the duty cycle. Drive the piezo through the original amplifier
  stage, or through a small NPN transistor (2N3904 with 1 kΩ base
  resistor, piezo from 5 V to the collector).

## Debug log

Open the board's USB serial port at any baud rate. You will see:

```
<- FD          byte from the DEC host
-> 01          byte sent to the host
kbd0: 02 00 04 00 00 00 00 00   raw USB boot report
kbd0 mounted / removed
LEDs 4         LK201 LED bits now lit
```

This log is the quickest way to compare against a real LK201 trace,
for example on the VT220 problem (issue #12). Set `LKDEBUG 0` to turn it
off.

## Keyboard compatibility

* Hubs work, including keyboards with a built-in hub (TinyUSB hub
  driver).
* Combo devices work (keyboard + media keys, wireless receivers). Only
  the boot-keyboard interface is used.
* Up to 4 keyboards can be connected at once through a hub. Their keys
  are merged.
* Keyboards that report their keys in sorted order, rather than in the
  order they were pressed, now work correctly.
