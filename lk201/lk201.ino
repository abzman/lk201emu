/*
 LK-201 emulator -- Adafruit Trinket M0 (SAMD21) USB host version

 Interfaces between a DEC terminal or workstation that expects an
 LK201-AA keyboard and a standard PC type USB keyboard.

 Copyright (c) 2020, Paul Koning

 Please read file LICENSE for the license that applies to this project.

 2026 rework: the LK201 protocol now lives in lk201_core.h (shared
 with the RP2040 version and unit tested on a PC).  This file is only
 the SAMD21 glue: USB host, 4800 baud UART, DAC beeper, DotStar LED.

 Behaviour changes vs. the original sketch:
  - The power-up report (01 00 00 00) is sent once at power-on and on
    host 0xFD, NOT every time the USB keyboard (re)enumerates.  The
    old behaviour made the host see a "new keyboard" whenever USB
    hiccuped, which shows up as constantly blinking LEDs (issue #6).
  - Nothing is written to the UART from the timer interrupt any more
    (autorepeat used to call Serial.write() inside the ISR, racing the
    main loop and corrupting output).  The ISR only does audio.
  - A USB hub driver is instantiated, so keyboards with a built-in hub
    can enumerate.
*/

#define DOIDENT 1   // Set to 0 to suppress power up ident

#include <Arduino.h>
#include "Adafruit_ZeroTimer.h"
#include <KeyboardController.h>
#include <hidboot.h>
#include <usbhub.h>
#include <Adafruit_DotStar.h>
#include <SPI.h>

#include "lk201_core.h"

// ---- DotStar status LED -------------------------------------------------
#define DATAPIN   7
#define CLOCKPIN  8
Adafruit_DotStar strip = Adafruit_DotStar(1, DATAPIN, CLOCKPIN, DOTSTAR_BGR);

static void setPixel(uint32_t c) { strip.setPixelColor(0, c); strip.show(); }

// ---- Host UART ------------------------------------------------------------
#if (USB_VID==0x2341 && defined(ARDUINO_SAMD_ZERO)) || (USB_VID==0x2a03 && defined(ARDUINO_SAM_ZERO))
#define HostSerial SERIAL_PORT_MONITOR
#else
#define HostSerial Serial1
#endif

// ---- Audio (DAC on A0 through the amplifier, 2 kHz) -------------------------
#define INTRATE 4000                     // 4 kHz interrupt -> 2 kHz tone
#define MS2DELAY(x)   ((x) * INTRATE / 1000)
#define WPM2DELAY(x)  MS2DELAY (1200 / (x))
#define APIN A0
#define DACFULL 1023
#define BIAS (DACFULL / 6)
#define FS (DACFULL - BIAS)
#define AOUT(x) ((int)(FS - (x)))
#define AOFF DACFULL
#define P 0.707
#define E(i, b, pp) ((i & (1 << b)) ? (pp): 1)
#define VOL(i) AOUT (FS * E(i, 2, P*P*P*P) * E(i, 1, P*P) * E(i, 0, P))
static const int vol[8] = { VOL(0), VOL(1), VOL(2), VOL(3), VOL(4), VOL(5), VOL(6), VOL(7) };

#define CLICKTIME 10                     // 5 cycles
#define BELLTIME  MS2DELAY(125)

static volatile int audioCounter;
static volatile int curVol;
static const int8_t * volatile idp;

#if DOIDENT
#define IDUNIT WPM2DELAY (30)
#define T 1
#define H (T * 3)
#define S (-T)
#define LS (-H)
static const int8_t ident[] = {
  H, S, T, S, T, LS, T, LS, LS, S,
  H, S, T, LS, T, S, T, LS,
  T, S, H, S, H, S, H, S, H, LS,
  H, S, T, S, T, 0
};
#endif
static const int8_t noIdent[] = { 0 };

static void startTone(int ticks, int v) {
  noInterrupts();
  idp = noIdent;
  curVol = v;
  audioCounter = ticks;
  interrupts();
}

Adafruit_ZeroTimer zerotimer = Adafruit_ZeroTimer(3);
void TC3_Handler() { Adafruit_ZeroTimer::timerHandler(3); }

// Audio only.  Never touches the UART.
void TimerCallback0(void) {
  if (!audioCounter) return;
  if (--audioCounter & 1) {
    analogWrite(APIN, curVol);
  } else {
    analogWrite(APIN, AOFF);
#if DOIDENT
    int id;
    if (audioCounter == 0 && (id = *idp) != 0) {
      ++idp;
      id *= IDUNIT;
      if (id > 0) { curVol = VOL(4); audioCounter = id; }
      else        { curVol = AOFF;   audioCounter = -id; }
    }
#endif
  }
}

static void setupTimer(int freq) {
  zerotimer.enable(false);
  zerotimer.configure(TC_CLOCK_PRESCALER_DIV1, TC_COUNTER_SIZE_16BIT,
                      TC_WAVE_GENERATION_MATCH_PWM);
  zerotimer.setCompare(0, 48000000 / freq);
  zerotimer.setCallback(true, TC_CALLBACK_CC_CHANNEL0, TimerCallback0);
  zerotimer.enable(true);
}

// ---- USB host ---------------------------------------------------------------
USBHost usb;
USBHub  hub1(&usb);          // keyboards with a built-in hub
HID    *kb_hid = NULL;

// ---- Glue between core and hardware -------------------------------------
static volatile uint8_t wantLeds = 0;

class TrinketHooks : public lk201::Hooks {
public:
  void tx(uint8_t b) override { HostSerial.write(b); }
  void leds(uint8_t l) override { wantLeds = l; }
  void click(uint8_t v) override { startTone(CLICKTIME, vol[v & 7]); }
  void bell(uint8_t v) override { startTone(BELLTIME, vol[v & 7]); }
};
static TrinketHooks hooks;
static lk201::Keyboard lk(hooks);

class KeyboardRaw : public KeyboardReportParser {
public:
  KeyboardRaw(USBHost &u) : hostKeyboard(&u) { hostKeyboard.SetReportParser(0, this); }
  void Parse(HID *hid, uint32_t is_rpt_id, uint32_t len, uint8_t *buf) override {
    (void)is_rpt_id;
    kb_hid = hid;
    lk.hidReport(0, buf, (int)len, micros());
  }
  HID *hid() { return &hostKeyboard; }
private:
  HIDBoot<HID_PROTOCOL_KEYBOARD> hostKeyboard;
};
KeyboardRaw keyboard(usb);

// Push LED state to the USB keyboard and the DotStar (main loop only).
static uint8_t usbLedsApplied = 0xff;
static uint8_t pixelLedsApplied = 0xff;
static bool usbRunning = false;

static void applyLeds() {
  const uint8_t l = wantLeds;
  if (usbRunning && kb_hid != NULL && l != usbLedsApplied) {
    uint8_t usbLeds = (l >> 1) & 0x07;   // Compose->Num, Lock->Caps, Hold->Scroll
    kb_hid->SetReport(0, 0, 2, 0, 1, &usbLeds);
    usbLedsApplied = l;
  }
  if (usbRunning && (l & lk201::LED_WAIT) != pixelLedsApplied) {
    pixelLedsApplied = l & lk201::LED_WAIT;
    setPixel(pixelLedsApplied ? 0x002800 : 0x000000);  // WAIT = green
  }
}

// ---- Arduino entry points -----------------------------------------------
void setup() {
  HostSerial.begin(4800);
  strip.begin();
  setPixel(0);

  setupTimer(INTRATE);
  analogWrite(APIN, AOFF);
#if DOIDENT
  idp = ident;
  audioCounter = 1;
#else
  idp = noIdent;
  audioCounter = 0;
#endif

  usb.Init();
  delay(20);

  // Tell the host a keyboard is here right away, like a real LK201.
  lk.begin(micros());
}

static uint32_t lastUSBstate = 0xffffffff;

void loop() {
  while (HostSerial.available())
    lk.hostByte((uint8_t)HostSerial.read(), micros());

  usb.Task();

  const uint32_t st = usb.getUsbTaskState();
  if (st != lastUSBstate) {
    lastUSBstate = st;
    const bool running = (st == USB_STATE_RUNNING);
    if (!running && usbRunning) {
      // Keyboard went away: release everything, forget the HID pointer
      kb_hid = NULL;
      lk.sourceGone(0, micros());
    }
    usbRunning = running;
    switch (st) {
    case USB_ATTACHED_SUBSTATE_SETTLE:          setPixel(0x280028); break; // purple
    case USB_DETACHED_SUBSTATE_WAIT_FOR_DEVICE: setPixel(0x000028); break; // blue
    case USB_ATTACHED_SUBSTATE_RESET_DEVICE:    setPixel(0x282800); break; // yellow
    case USB_ATTACHED_SUBSTATE_WAIT_RESET_COMPLETE: setPixel(0x281400); break; // orange
    case USB_STATE_CONFIGURING:                 setPixel(0x002828); break; // cyan
    case USB_STATE_RUNNING:
      setPixel(0x005500);                       // bright green flash
      delay(50);
      setPixel(0);
      kb_hid = keyboard.hid();                  // LEDs work before first keypress
      usbLedsApplied = pixelLedsApplied = 0xff; // re-send LED state
      break;
    }
  }

  lk.poll(micros());
  applyLeds();
}
