// Host-side tests for common/lk201_core.h.   Build & run:  make -C test
#include "../common/lk201_core.h"
#include <cstdio>
#include <vector>
#include <string>
#include <initializer_list>

using namespace lk201;

struct FakeHw : Hooks {
  std::vector<uint8_t> out;
  int clicks = 0, bells = 0; uint8_t lastLeds = 0; int ledCalls = 0;
  void tx(uint8_t b) override { out.push_back(b); }
  void leds(uint8_t l) override { lastLeds = l; ledCalls++; }
  void click(uint8_t) override { clicks++; }
  void bell(uint8_t) override { bells++; }
  std::vector<uint8_t> take() { auto o = out; out.clear(); return o; }
};

static int fails = 0, checks = 0;
static std::string hex(const std::vector<uint8_t> &v) {
  std::string s; char b[8];
  for (auto x : v) { snprintf(b, sizeof b, "%02X ", x); s += b; }
  return s;
}
static void expect(const char *what, const std::vector<uint8_t> &got,
                   std::initializer_list<uint8_t> want) {
  checks++;
  std::vector<uint8_t> w(want);
  if (got != w) { fails++; printf("FAIL %s\n  got:  %s\n  want: %s\n", what, hex(got).c_str(), hex(w).c_str()); }
}
static void expectTrue(const char *what, bool ok) { checks++; if (!ok) { fails++; printf("FAIL %s\n", what); } }

static void rep(Keyboard &k, uint32_t t, uint8_t mod, std::initializer_list<uint8_t> keys, int src = 0) {
  uint8_t r[8] = { mod, 0, 0, 0, 0, 0, 0, 0 };
  int i = 2; for (auto x : keys) r[i++] = x;
  k.hidReport(src, r, 8, t);
}

// usages
enum { U_A = 4, U_B = 5, U_C = 6, U_ISO_HASH = 0x32, U_ISO_BSL = 0x64, U_F13 = 0x68, U_F20 = 0x6F };
enum { LK_A = 194, LK_B = 217, LK_C = 206 };

int main() {
  FakeHw hw; Keyboard k(hw);
  uint32_t t = 1000;

  k.begin(t);
  expect("power-up report", hw.take(), {0x01, 0x00, 0x00, 0x00});
  expectTrue("LEDs flash during self test", hw.lastLeds == 0x0f);
  k.poll(t + 200000);
  expectTrue("LEDs off after self test", hw.lastLeds == 0x00);
  t += 200000;

  // Plain key: down sends code once, up sends nothing (down-only/AR key, no ALL UPS)
  rep(k, t, 0, {U_A}); rep(k, t + 10, 0, {});
  expect("single key A", hw.take(), {LK_A});

  // Keyboard that SORTS its key array: press B, then A while holding B.
  // Old code emitted B-up/B-down here (duplicate B).
  rep(k, t, 0, {U_B}); rep(k, t + 10, 0, {U_A, U_B}); rep(k, t + 20, 0, {U_A}); rep(k, t + 30, 0, {});
  expect("sorted-order keyboard, no phantom B", hw.take(), {LK_B, LK_A});

  // Key moves slot when an earlier key is released (press-order keyboard)
  rep(k, t, 0, {U_A, U_B, U_C}); rep(k, t + 5, 0, {U_B, U_C}); rep(k, t + 9, 0, {});
  expect("three keys", hw.take(), {LK_A, LK_B, LK_C});

  // Shift alone: down, then ALL UPS
  rep(k, t, 0x02, {}); rep(k, t + 5, 0x00, {});
  expect("shift alone", hw.take(), {KEY_SHIFT, ALL_UPS});

  // Shift + a, release a first, then shift
  rep(k, t, 0x02, {}); rep(k, t + 5, 0x02, {U_A}); rep(k, t + 9, 0x02, {}); rep(k, t + 12, 0, {});
  expect("shift+a", hw.take(), {KEY_SHIFT, LK_A, ALL_UPS});

  // Shift + Ctrl: first release sends the key code, last sends ALL UPS
  rep(k, t, 0x02, {}); rep(k, t + 1, 0x03, {}); rep(k, t + 2, 0x01, {}); rep(k, t + 3, 0, {});
  expect("shift+ctrl", hw.take(), {KEY_SHIFT, KEY_CTRL, KEY_SHIFT, ALL_UPS});

  // Both shifts: one LK shift; releasing one of them sends nothing
  rep(k, t, 0x22, {}); rep(k, t + 1, 0x20, {}); rep(k, t + 2, 0, {});
  expect("both shifts", hw.take(), {KEY_SHIFT, ALL_UPS});

  // Releasing a letter while shift is still held must NOT send ALL UPS
  rep(k, t, 0x02, {U_A}); rep(k, t + 1, 0x02, {});
  expect("no ALL UPS while shift held", hw.take(), {KEY_SHIFT, LK_A});
  rep(k, t + 2, 0, {}); hw.take();

  // ISO keys and F13/F20 now map; unknown usage ignored without garbage
  rep(k, t, 0, {U_ISO_BSL}); rep(k, t + 1, 0, {});
  rep(k, t, 0, {U_ISO_HASH}); rep(k, t + 1, 0, {});
  rep(k, t, 0, {U_F13}); rep(k, t + 1, 0, {});
  rep(k, t, 0, {U_F20}); rep(k, t + 1, 0, {});
  rep(k, t, 0, {0x87}); rep(k, t + 1, 0, {0xE0}); rep(k, t + 1, 0, {0xFF}); rep(k, t + 2, 0, {});
  expect("ISO + F13..F20, unknown ignored", hw.take(), {201, 247, 115, 131});

  // Every mapped code has a division
  for (unsigned u = 4; u < sizeof usb_lk201; u++)
    if (usb_lk201[u]) expectTrue("mapped key has division", divisionOf(usb_lk201[u]) != 0);
  for (int b = 0; b < 8; b++) expectTrue("modifier has division", divisionOf(usbmod_lk201[b]) != 0);

  // Rollover report is ignored (keeps previous state)
  rep(k, t, 0, {U_A}); rep(k, t + 1, 0, {1, 1, 1, 1, 1, 1}); rep(k, t + 2, 0, {U_A}); rep(k, t + 3, 0, {});
  expect("rollover ignored", hw.take(), {LK_A});

  // Autorepeat: main keypad uses buffer 0: 500 ms, 30 Hz
  t = 10000000;
  rep(k, t, 0, {U_A});
  for (uint32_t dt = 0; dt <= 590000; dt += 1000) k.poll(t + dt);
  // 500ms first, then every 33.333ms: at 500, 533, 567 (600 is past the window)
  expect("autorepeat metronome", hw.take(), {LK_A, METRONOME, METRONOME, METRONOME});
  rep(k, t + 600000, 0, {});
  for (uint32_t dt = 600000; dt <= 1500000; dt += 1000) k.poll(t + dt);
  expect("repeat stops on release", hw.take(), {});

  // Return is down-only: never repeats
  rep(k, t, 0, {0x28});
  for (uint32_t dt = 0; dt <= 1500000; dt += 1000) k.poll(t + dt);
  rep(k, t + 1500000, 0, {});
  expect("Return doesn't repeat", hw.take(), {189});

  // Host: request ID -> 01 00
  k.hostByte(0xAB, t);
  expect("request ID", hw.take(), {0x01, 0x00});

  // Host: mode set, division 1 -> down only.  op = 1<<3 | 0<<1, end bit
  k.hostByte(0x88, t);
  expect("mode change ack", hw.take(), {MODE_CHANGE_ACK});
  rep(k, t, 0, {U_A});
  for (uint32_t dt = 0; dt <= 1000000; dt += 1000) k.poll(t + dt);
  rep(k, t + 1000000, 0, {});
  expect("div1 down-only: no repeat", hw.take(), {LK_A});
  k.hostByte(0xD3, t);  // reinstate defaults
  expect("defaults: no reply", hw.take(), {});

  // Mode set with AR buffer parameter: div 1 autorepeat (m=1), buffer 3
  k.hostByte(0x0A, t); k.hostByte(0x83, t);
  expect("mode set with param", hw.take(), {MODE_CHANGE_ACK});
  rep(k, t, 0, {U_A});
  for (uint32_t dt = 0; dt <= 300000; dt += 1000) k.poll(t + dt);  // buffer 3 = 300 ms
  rep(k, t + 300000, 0, {});
  expect("AR buffer 3 timing", hw.take(), {LK_A, METRONOME});

  // AR rate set: buffer 0 -> 100 ms, 20 Hz.   op = 0x0f<<3 | 0<<1 = 0x78
  k.hostByte(0xD3, t);
  k.hostByte(0x78, t); k.hostByte(20, t); k.hostByte(0x80 | 20, t);
  expect("rate set: no ack", hw.take(), {});
  rep(k, t, 0, {U_A});
  for (uint32_t dt = 0; dt <= 150000; dt += 1000) k.poll(t + dt);  // 100, 150
  rep(k, t + 150000, 0, {});
  expect("new AR rate", hw.take(), {LK_A, METRONOME, METRONOME});
  k.hostByte(0xD3, t);

  // Disable autorepeat across keyboard, then re-enable
  k.hostByte(0xE1, t);
  rep(k, t, 0, {U_A}); for (uint32_t dt = 0; dt <= 900000; dt += 1000) k.poll(t + dt); rep(k, t + 900000, 0, {});
  k.hostByte(0xE3, t);
  expect("E1 disables repeat", hw.take(), {LK_A});

  // LEDs on/off
  k.hostByte(0x13, t); k.hostByte(0x80 | LED_LOCK | LED_HOLD, t);
  expectTrue("LEDs on", hw.lastLeds == (LED_LOCK | LED_HOLD));
  k.hostByte(0x11, t); k.hostByte(0x80 | LED_HOLD, t);
  expectTrue("LED off", hw.lastLeds == LED_LOCK);
  k.hostByte(0x11, t); k.hostByte(0x8F, t);
  expectTrue("all LEDs off", hw.lastLeds == 0);
  expect("LED cmds silent", hw.take(), {});

  // Inhibit: ack B7, WAIT LED on, keystrokes held (4 max), resume flushes
  k.hostByte(0x89, t);
  expect("inhibit ack", hw.take(), {KBD_LOCKED_ACK});
  expectTrue("WAIT LED when locked", hw.lastLeds & LED_WAIT);
  rep(k, t, 0, {U_A}); rep(k, t + 1, 0, {}); rep(k, t + 2, 0, {U_B}); rep(k, t + 3, 0, {});
  expect("nothing sent while locked", hw.take(), {});
  k.hostByte(0x8B, t);
  expect("resume flushes held keys", hw.take(), {LK_A, LK_B});
  expectTrue("WAIT LED off after resume", !(hw.lastLeds & LED_WAIT));

  k.hostByte(0x89, t); hw.take();
  for (int i = 0; i < 3; i++) { rep(k, t, 0, {U_A}); rep(k, t + 1, 0, {}); rep(k, t, 0, {U_B}); rep(k, t + 1, 0, {}); }
  k.hostByte(0x8B, t);
  expect("overflow -> OUTPUT ERROR then held", hw.take(), {OUTPUT_ERROR, LK_A, LK_B, LK_A, LK_B});

  // Click / bell
  hw.clicks = hw.bells = 0;
  k.hostByte(0xA7, t); k.hostByte(0xA1, t); k.hostByte(0xA7, t); k.hostByte(0x23, t); k.hostByte(0x83, t); k.hostByte(0xA7, t);
  expectTrue("bell enable/disable", hw.bells == 2);
  k.hostByte(0x99, t); rep(k, t, 0, {U_A}); rep(k, t, 0, {});
  expectTrue("click off", hw.clicks == 0);
  k.hostByte(0x1B, t); k.hostByte(0x81, t); rep(k, t, 0, {U_A}); rep(k, t, 0, {});
  expectTrue("click on", hw.clicks == 1);
  rep(k, t, 0x01, {}); rep(k, t, 0, {});
  expectTrue("no ctrl click by default", hw.clicks == 1);
  hw.take();

  // Unknown peripheral command -> INPUT ERROR; test mode -> B8
  k.hostByte(0xF1, t);
  k.hostByte(0xCB, t);
  expect("unknown / test mode", hw.take(), {INPUT_ERROR, TEST_MODE_ACK});

  // 0xFD in the middle of a command still resets
  k.hostByte(0x13, t); k.hostByte(0xFD, t);
  expect("FD mid-command", hw.take(), {0x01, 0x00, 0x00, 0x00});
  k.poll(t + 200000);

  // Two keyboards: same key held on both -> one down; gone source releases
  rep(k, t, 0x02, {}, 0); rep(k, t, 0x02, {U_A}, 1);
  rep(k, t, 0x00, {}, 0);              // shift released on kb0 only
  k.sourceGone(1, t);                  // kb1 unplugged with A held
  expect("two sources", hw.take(), {KEY_SHIFT, LK_A, ALL_UPS});

  // Short / bogus report lengths are ignored
  uint8_t shortr[4] = {0, 0, U_A, 0};
  k.hidReport(0, shortr, 4, t);
  expect("short report ignored", hw.take(), {});

  printf("%d checks, %d failures\n", checks, fails);
  return fails ? 1 : 0;
}
