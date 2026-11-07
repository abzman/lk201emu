/*
 LK201 protocol core — hardware independent.

 Shared by the Trinket M0 (SAMD21) sketch and the RP2040/RP2350 sketch.
 It can also be compiled on a PC for unit testing (see test/).

 Copyright (c) 2020, Paul Koning.  Refactoring and fixes 2026.
 Please read file LICENSE for the license that applies to this project.

 NOTE: the master copy lives in common/lk201_core.h.  The Arduino IDE
 cannot include files from outside the sketch folder, so identical
 copies sit in lk201/ and lk201_rp2040/.  Edit the master and run
 "make sync" in test/ (the tests fail if the copies drift).

 What this fixes compared with the original single-file sketch:
  * USB report diffing is set based.  The old merge assumed keys stay
    in the same slot order between reports; keyboards that sort the
    key array by usage code produced phantom key-up/key-down pairs
    (doubled characters, "B" panics, stuck autorepeat).
  * Unmapped scan codes no longer index divs[] out of bounds (the
    old code did divs[0 - 86] for ISO keys, sending garbage).
  * ALL UPS (0xB3) is sent only when the last up/down-mode key is
    released, as on a real LK201, instead of after every keystroke.
  * Request ID (0xAB) answers 0x01 0x00 (LK201), not 0x00 0x00.
  * Inhibit / resume transmission (0x89 / 0x8B) are implemented with
    the KBD LOCKED ACK (0xB7) the VT220 waits for, plus the 4-byte
    hold buffer and OUTPUT ERROR (0xB5) on overflow.
  * Added: 0xE1/0xE3 autorepeat off/on, 0xD9 autorepeat->down only,
    0xD3 reinstate defaults, 0xCB test mode (acked), unknown -> 0xB6.
  * ISO keys (Non-US # and the < > key left of Z) and F13..F20 are
    mapped.  PF4 (0xA4) now has its keypad division.
  * Several keyboards (or several keyboard interfaces on one device)
    are merged, and a disconnect releases every key it held.
  * Nothing here runs in interrupt context.
*/

#pragma once
#include <stdint.h>
#include <string.h>

namespace lk201 {

// ---- Codes sent to the host -------------------------------------------
enum : uint8_t {
  ALL_UPS        = 0xB3,
  METRONOME      = 0xB4,
  OUTPUT_ERROR   = 0xB5,
  INPUT_ERROR    = 0xB6,
  KBD_LOCKED_ACK = 0xB7,
  TEST_MODE_ACK  = 0xB8,
  MODE_CHANGE_ACK= 0xBA,
  KEY_SHIFT      = 174,
  KEY_CTRL       = 175,
};

// ---- LED bits (LK201 numbering) ----------------------------------------
enum : uint8_t { LED_WAIT = 1, LED_COMPOSE = 2, LED_LOCK = 4, LED_HOLD = 8 };

// ---- Division modes ----------------------------------------------------
enum : uint8_t { MODE_DOWN = 0, MODE_AUTOREPEAT = 1, MODE_UPDOWN = 3 };

// Keyboard division for each LK201 keycode, starting at DIVBASE.
static const uint8_t DIVBASE = 86;
static const uint8_t divs[] = {
  10, 10, 10, 10, 10, 0, 0, 0, 0, 0, 0, 0, 0, 0,
  11, 11, 11, 11, 11, 0, 0, 0, 0, 0, 0, 0, 0,
  12, 12, 12, 12, 0, 0, 0, 0, 0, 0, 0, 13, 13, 0, 0,
  14, 14, 14, 14, 0, 0, 0, 0, 0, 0, 9, 9, 9, 9, 9, 9,
  0, 0, 2, 0, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2,
  2 /* PF4, was 0 */, 0, 0, 7, 7, 8, 8, 0, 0, 0, 6, 6, 5, 5, 0, 0, 0, 0,
  0, 0, 0, 0, 0, 0, 3, 4, 4, 1, 1, 1, 1, 1, 0, 1, 1, 1, 1, 1,
  0, 1, 1, 1, 1, 0, 1, 1, 1, 1, 1, 0, 1, 1, 1, 1, 0,
  1, 1, 1, 1, 0, 1, 1, 1, 1, 0, 1, 1, 1, 1, 0, 1, 1, 1, 1, 0,
  1, 1, 0, 1, 1, 0, 1, 1, 1, 0, 1, 1, 1
};

inline uint8_t divisionOf(uint8_t lk) {
  if (lk < DIVBASE || lk >= DIVBASE + sizeof(divs)) return 0;
  return divs[lk - DIVBASE];
}

// USB HID usage (boot keyboard) -> LK201 keycode.  0 = not mapped.
// Mapping is by key position; see doc/emulator.md.
static const uint8_t usb_lk201[] = {
  0, 0, 0, 0,                               // 00-03 none/rollover/err/res
  194, 217, 206, 205, 204, 210, 216, 221,   // A B C D E F G H
  230, 226, 231, 236, 227, 222, 235, 240,   // I J K L M N O P
  193, 209, 199, 215, 225, 211, 198, 200,   // Q R S T U V W X
  220, 195,                                 // Y Z
  192, 197, 203, 208, 214, 219, 224, 229, 234, 239,  // 1..0
  189,  // 28 Enter
  113,  // 29 Esc -> F11
  188,  // 2A Backspace (Delete)
  190,  // 2B Tab
  212,  // 2C Space
  249,  // 2D - _
  245,  // 2E = +
  250,  // 2F [ {
  246,  // 30 ] }
  247,  // 31 \ |
  247,  // 32 Non-US # ~ (ISO key left of Enter) -> same position   [new]
  242,  // 33 ; :
  251,  // 34 ' "
  191,  // 35 ` ~
  232,  // 36 , <
  237,  // 37 . >
  243,  // 38 / ?
  176,  // 39 Caps Lock -> Lock
  86, 87, 88, 89, 90,                       // F1-F5
  100, 101, 102, 103, 104,                  // F6-F10
  113, 114,                                 // F11 F12
  116,  // 46 PrtScr -> F14
  124,  // 47 Scroll Lock -> Help
  125,  // 48 Pause -> Do
  138,  // 49 Insert -> Find
  139,  // 4A Home -> Insert Here
  140,  // 4B PgUp -> Remove
  141,  // 4C Delete -> Select
  142,  // 4D End -> Prev Screen
  143,  // 4E PgDn -> Next Screen
  168, 167, 169, 170,                       // Right Left Down Up
  161,  // 53 Num Lock -> PF1
  162,  // 54 KP / -> PF2
  163,  // 55 KP * -> PF3
  160,  // 56 KP -
  156,  // 57 KP + -> KP ,
  149,  // 58 KP Enter
  150, 151, 152, 153, 154, 155, 157, 158, 159,  // KP 1-9
  146,  // 62 KP 0
  148,  // 63 KP .
  201,  // 64 Non-US \ | (ISO key left of Z) -> < >                 [new]
  130,  // 65 Application -> F19
  0,    // 66 Power
  0,    // 67 KP =
  115,  // 68 F13                                                    [new]
  116,  // 69 F14
  124,  // 6A F15 -> Help
  125,  // 6B F16 -> Do
  128,  // 6C F17
  129,  // 6D F18
  130,  // 6E F19
  131,  // 6F F20
};

// Modifier bits 0..7 (LCtrl LShift LAlt LGUI RCtrl RShift RAlt RGUI)
static const uint8_t usbmod_lk201[8] = {
  KEY_CTRL,   // Left Ctrl
  KEY_SHIFT,  // Left Shift
  177,        // Left Alt -> Compose
  201,        // Left Windows -> < >
  131,        // Right Ctrl -> F20
  KEY_SHIFT,  // Right Shift -> Shift
  128,        // Right Alt -> F17
  129,        // Right Windows -> F18
};

// Defaults (division 0 is internal; divisions are 1..14)
static const uint8_t defmodes[15] = {
  MODE_UPDOWN,
  MODE_AUTOREPEAT, MODE_AUTOREPEAT, MODE_AUTOREPEAT,   // 1 main, 2 keypad, 3 delete
  MODE_DOWN, MODE_DOWN,                                // 4 return/tab, 5 lock/compose
  MODE_UPDOWN,                                         // 6 shift/ctrl
  MODE_AUTOREPEAT, MODE_AUTOREPEAT,                    // 7,8 cursor
  MODE_UPDOWN,                                         // 9 editing
  MODE_DOWN, MODE_DOWN, MODE_DOWN, MODE_DOWN, MODE_DOWN // 10-14 function keys
};
static const uint8_t defar[15] = { 0, 0, 0, 1, 0, 0, 0, 1, 1, 0, 0, 0, 0, 0, 0 };

struct ArBuf { uint32_t timeout_us; uint32_t interval_us; };
static const ArBuf defarbufs[4] = {
  { 500000, 1000000 / 30 },
  { 300000, 1000000 / 30 },
  { 500000, 1000000 / 40 },
  { 300000, 1000000 / 40 },
};

// ---- Hardware hooks ----------------------------------------------------
class Hooks {
public:
  virtual void tx(uint8_t b) = 0;          // one byte to the host UART
  virtual void leds(uint8_t lkLeds) = 0;   // desired LK201 LED bits
  virtual void click(uint8_t vol) = 0;     // vol 0 = loudest .. 7
  virtual void bell(uint8_t vol) = 0;
};

// ---- The emulator ------------------------------------------------------
class Keyboard {
public:
  static const int NSRC = 4;      // keyboards / HID interfaces merged
  static const int MAXDOWN = 32;

  explicit Keyboard(Hooks &h) : hw(h) { reset(0); }

  // Call once at power-up.  Sends the power-up self-test report.
  void begin(uint32_t now_us) { reset(now_us); powerUpReport(now_us); }

  // Feed every byte received from the host.
  void hostByte(uint8_t b, uint32_t now_us);

  // Feed an 8-byte boot-protocol keyboard report from source 'src'.
  void hidReport(int src, const uint8_t *r, int len, uint32_t now_us);

  // A keyboard (source) disappeared: release everything it held.
  void sourceGone(int src, uint32_t now_us) {
    if (src < 0 || src >= NSRC) return;
    srcCnt[src] = 0;
    update(now_us);
  }

  // Call often (every loop).  Drives autorepeat and the LED flash.
  void poll(uint32_t now_us);

  uint8_t currentLeds() const {
    if (flashing) return 0x0f;
    return leds | (locked ? LED_WAIT : 0);
  }

private:
  Hooks &hw;

  uint8_t modes[15], ar[15];
  ArBuf arbufs[4];
  bool arEnabled;

  bool clickOff, ctrlClick, bellOff;
  uint8_t clickVol, bellVol;

  uint8_t leds;
  bool flashing; uint32_t flashEnd;
  uint8_t lastLedsSent;

  bool locked, holdOverflow;
  uint8_t hold[4]; int holdCnt;

  uint8_t cmd[4]; int cmdcnt; bool cmdOverflow;

  // Keys per source, and the merged set in press order (oldest first)
  uint8_t srcKeys[NSRC][16]; int srcCnt[NSRC];
  uint8_t down[MAXDOWN]; int downCnt;

  // Autorepeat
  bool repActive; uint8_t repKey; bool repSendKey;
  uint32_t repDue, repInterval;

  void reset(uint32_t now_us) {
    memcpy(modes, defmodes, sizeof modes);
    memcpy(ar, defar, sizeof ar);
    memcpy(arbufs, defarbufs, sizeof arbufs);
    arEnabled = true;
    clickOff = ctrlClick = bellOff = false;
    clickVol = bellVol = 2;
    leds = 0; flashing = false; flashEnd = now_us; lastLedsSent = 0xff;
    locked = holdOverflow = false; holdCnt = 0;
    cmdcnt = 0; cmdOverflow = false;
    // Forget key state; anything still held is reported as a fresh
    // press with the next USB report.
    downCnt = 0;
    for (int s = 0; s < NSRC; s++) srcCnt[s] = 0;
    repActive = false;
  }

  void powerUpReport(uint32_t now_us) {
    // Real LK201 lights all LEDs during self test.
    flashing = true; flashEnd = now_us + 100000;
    pushLeds();
    raw(0x01); raw(0x00); raw(0x00); raw(0x00);
  }

  void raw(uint8_t b) { hw.tx(b); }

  // Keystroke data: subject to inhibit / resume
  void send(uint8_t b) {
    if (!locked) { hw.tx(b); return; }
    if (holdCnt < (int)sizeof hold) hold[holdCnt++] = b;
    else holdOverflow = true;
  }

  void pushLeds() {
    uint8_t l = currentLeds();
    if (l != lastLedsSent) { lastLedsSent = l; hw.leds(l); }
  }

  static bool contains(const uint8_t *a, int n, uint8_t v) {
    for (int i = 0; i < n; i++) if (a[i] == v) return true;
    return false;
  }

  void startRepeat(uint8_t lk, bool sendKeyFirst, uint32_t now_us) {
    const uint8_t div = divisionOf(lk);
    if (!arEnabled || modes[div] != MODE_AUTOREPEAT) return;
    const ArBuf &b = arbufs[ar[div] & 3];
    repActive = true; repKey = lk; repSendKey = sendKeyFirst;
    repDue = now_us + b.timeout_us; repInterval = b.interval_us;
  }

  void keyDown(uint8_t lk, uint32_t now_us) {
    repActive = false;           // any new keystroke ends autorepeat
    send(lk);
    if (lk != KEY_SHIFT && (lk != KEY_CTRL || ctrlClick) && !clickOff)
      hw.click(clickVol);
    startRepeat(lk, false, now_us);
  }

  // 'remaining' = keys still down after this one went up
  void keyUp(uint8_t lk, uint32_t now_us) {
    const uint8_t div = divisionOf(lk);
    repActive = false;
    if (modes[div] == MODE_UPDOWN) {
      bool otherUD = false;
      for (int i = 0; i < downCnt; i++)
        if (modes[divisionOf(down[i])] == MODE_UPDOWN) { otherUD = true; break; }
      send(otherUD ? lk : (uint8_t)ALL_UPS);
      return;
    }
    // Down-only / autorepeat key released: the most recent key still
    // down resumes repeating (original emulator behaviour).
    for (int i = downCnt - 1; i >= 0; i--) {
      if (modes[divisionOf(down[i])] != MODE_UPDOWN) {
        startRepeat(down[i], true, now_us);
        break;
      }
    }
  }

  // Recompute merged key set and emit transitions.
  void update(uint32_t now_us) {
    uint8_t want[MAXDOWN]; int wn = 0;
    for (int s = 0; s < NSRC; s++)
      for (int i = 0; i < srcCnt[s]; i++)
        if (wn < MAXDOWN && !contains(want, wn, srcKeys[s][i]))
          want[wn++] = srcKeys[s][i];

    // Releases first, oldest first
    for (int i = 0; i < downCnt; ) {
      uint8_t k = down[i];
      if (!contains(want, wn, k)) {
        memmove(down + i, down + i + 1, downCnt - i - 1);
        downCnt--;
        keyUp(k, now_us);
      } else i++;
    }
    // Then presses, in report order
    for (int i = 0; i < wn; i++) {
      uint8_t k = want[i];
      if (!contains(down, downCnt, k) && downCnt < MAXDOWN) {
        down[downCnt++] = k;
        keyDown(k, now_us);
      }
    }
  }

  void execute(uint32_t now_us);
};

inline void Keyboard::hidReport(int src, const uint8_t *r, int len, uint32_t now_us) {
  if (src < 0 || src >= NSRC || len < 8) return;
  // Phantom / rollover state: all key slots = 0x01.  Ignore the report.
  if (r[2] == 0x01) return;

  uint8_t keys[16]; int n = 0;
  for (int b = 0; b < 8; b++)
    if (r[0] & (1 << b)) {
      uint8_t lk = usbmod_lk201[b];
      if (lk && divisionOf(lk) && !contains(keys, n, lk)) keys[n++] = lk;
    }
  for (int i = 2; i < 8; i++) {
    uint8_t u = r[i];
    if (u < 4 || u >= sizeof usb_lk201) continue;
    uint8_t lk = usb_lk201[u];
    if (lk && divisionOf(lk) && !contains(keys, n, lk) && n < 16) keys[n++] = lk;
  }
  memcpy(srcKeys[src], keys, n);
  srcCnt[src] = n;
  update(now_us);
}

inline void Keyboard::hostByte(uint8_t b, uint32_t now_us) {
  if (b == 0xFD) {               // Jump to power-up: always honoured at once
    reset(now_us);
    powerUpReport(now_us);
    return;
  }
  if (cmdcnt < (int)sizeof cmd) cmd[cmdcnt++] = b;
  else cmdOverflow = true;
  if ((b & 0x80) == 0) return;   // more parameters follow
  if (cmdOverflow) raw(INPUT_ERROR);
  else execute(now_us);
  cmdcnt = 0; cmdOverflow = false;
}

inline void Keyboard::execute(uint32_t /*now_us*/) {
  const uint8_t op = cmd[0];
  const int np = cmdcnt - 1;

  if ((op & 1) == 0) {
    // Transmission commands: mode set / autorepeat rate set
    const int div = (op >> 3) & 0x0f;
    const int m   = (op >> 1) & 0x03;
    if (div == 0x0f) {
      if (np >= 2) {
        arbufs[m].timeout_us = (uint32_t)(cmd[1] & 0x7f) * 5000;
        int rate = cmd[2] & 0x7f;
        if (rate < 12) rate = 12;
        arbufs[m].interval_us = 1000000 / rate;
      }
      return;                    // no ack for rate set
    }
    if (div >= 1 && div <= 14 && m != 2) {
      modes[div] = m;
      if (np >= 1) ar[div] = cmd[1] & 0x03;
    }
    raw(MODE_CHANGE_ACK);
    return;
  }

  // Peripheral commands
  switch (op) {
  case 0x8B:                     // Resume keyboard transmission
    locked = false;
    if (holdOverflow) raw(OUTPUT_ERROR);
    for (int i = 0; i < holdCnt; i++) raw(hold[i]);
    holdCnt = 0; holdOverflow = false;
    break;
  case 0x89:                     // Inhibit keyboard transmission
    locked = true;
    repActive = false;
    raw(KBD_LOCKED_ACK);
    break;
  case 0x13: if (np >= 1) leds |= (cmd[1] & 0x0f); break;   // LEDs on
  case 0x11: if (np >= 1) leds &= ~(cmd[1] & 0x0f); break;  // LEDs off
  case 0x99: clickOff = true; break;
  case 0x1B: clickOff = false; if (np >= 1) clickVol = cmd[1] & 7; break;
  case 0xB9: ctrlClick = false; break;
  case 0xBB: ctrlClick = true; break;
  case 0x9F: if (!clickOff) hw.click(clickVol); break;
  case 0xA1: bellOff = true; break;
  case 0x23: bellOff = false; if (np >= 1) bellVol = cmd[1] & 7; break;
  case 0xA7: if (!bellOff) hw.bell(bellVol); break;
  case 0xC1: repActive = false; break;          // inhibit AR, current key
  case 0xE1: arEnabled = false; repActive = false; break;
  case 0xE3: arEnabled = true; break;
  case 0xD9:                                    // all AR divisions -> down only
    for (int d = 1; d < 15; d++)
      if (modes[d] == MODE_AUTOREPEAT) modes[d] = MODE_DOWN;
    repActive = false;
    break;
  case 0xAB: raw(0x01); raw(0x00); break;       // keyboard ID: LK201
  case 0xD3:                                    // reinstate defaults
    memcpy(modes, defmodes, sizeof modes);
    memcpy(ar, defar, sizeof ar);
    memcpy(arbufs, defarbufs, sizeof arbufs);
    arEnabled = true;
    break;
  case 0xCB: raw(TEST_MODE_ACK); break;         // test mode: ack, ignore
  default:  raw(INPUT_ERROR); break;
  }
  pushLeds();
}

inline void Keyboard::poll(uint32_t now_us) {
  if (flashing && (int32_t)(now_us - flashEnd) >= 0) flashing = false;
  pushLeds();
  if (repActive && !locked && (int32_t)(now_us - repDue) >= 0) {
    send(repSendKey ? repKey : (uint8_t)METRONOME);
    repSendKey = false;
    repDue += repInterval;
    if ((int32_t)(now_us - repDue) >= 0) repDue = now_us + repInterval; // no bursts
  }
}

} // namespace lk201
