/*
 LK-201 emulator -- RP2040 / RP2350 version (USB host via PIO-USB)

 Emulates a DEC LK201 keyboard using a standard USB keyboard.

 Tested target boards (pick it in Tools > Board):
   * Adafruit Feather RP2040 with USB Type A Host (Adafruit #5723)
   * Waveshare RP2350-USB-A  (choose "Waveshare RP2350 USB-A" if your
     core version lists it, otherwise "Raspberry Pi Pico 2" and set
     USB_HOST_DP below to 12)
   * Any Pico / Pico 2 with a USB-A jack wired to two adjacent GPIOs

 Build requirements (Arduino IDE / arduino-cli):
   * Board package: "Raspberry Pi Pico/RP2040/RP2350" by Earle Philhower
   * Libraries: "Adafruit TinyUSB Library", "Pico PIO USB"
   * Tools > CPU Speed: 120 MHz (or 240 MHz) -- PIO-USB needs a
     multiple of 12 MHz
   * Tools > USB Stack: "Adafruit TinyUSB"

 Core 1 runs the USB host (PIO-USB + TinyUSB); core 0 runs the LK201
 protocol, the 4800 baud link to the DEC host and the beeper.  The
 native USB-C port stays free for programming (hold BOOT, drag the
 .uf2) and for a debug log on the USB serial port.

 Copyright (c) 2020, Paul Koning.  RP2040 port 2026.
 Please read file LICENSE for the license that applies to this project.
*/

#include <Arduino.h>
#include "Adafruit_TinyUSB.h"
#include "pio_usb.h"
#include "pico/util/queue.h"
#include <math.h>

#include "lk201_core.h"

// ======================= Configuration =======================

#define DOIDENT 1          // Morse "ident" beep at power-up
#define LKDEBUG 1          // Log traffic on the USB-C serial port

// USB host D+ pin (D- must be D+ + 1)
#ifdef PIN_USB_HOST_DP
#define USB_HOST_DP PIN_USB_HOST_DP     // Feather RP2040 USB Host: 16
#else
#define USB_HOST_DP 12                  // Waveshare RP2350-USB-A
#endif

// UART to the DEC host (through the RS-232/423 level shifter).
// UART0 can use GPIO 0/1, 12/13, 16/17 for TX/RX; UART1 4/5, 8/9.
#define LK_TX_PIN 0
#define LK_RX_PIN 1

// Beeper: PWM output, 2 kHz (piezo resonance), volume via duty cycle
#define BEEP_PIN  5

// WAIT LED (the 4th LK201 LED; the other three go to the USB keyboard)
#ifdef LED_BUILTIN
#define WAIT_LED  LED_BUILTIN
#endif

// ======================= Debug log ===========================

#if LKDEBUG
#define DBG(...) do { if (Serial) Serial.printf(__VA_ARGS__); } while (0)
#else
#define DBG(...) do {} while (0)
#endif

// ======================= Core 1 <-> core 0 ===================

struct UsbEvent {
  uint8_t type;        // 0 = report, 1 = mounted, 2 = unmounted
  uint8_t src;         // keyboard slot 0..NSRC-1
  uint8_t len;
  uint8_t data[8];
};
static queue_t usbEvents;

static volatile uint8_t wantUsbLeds = 0;   // written by core 0, read by core 1

// ======================= Beeper ==============================

static const uint32_t TONE_HZ = 2000;
static const int PWM_RANGE = 1000;
static int dutyForVol[8];

static bool toneOn = false;
static uint32_t toneEnd;

#if DOIDENT
// Morse ident: >0 = tone units, <0 = silence units, 0 = end
static const int8_t ident[] = {
  3, -1, 1, -1, 1, -3, 1, -3, -3, -1,
  3, -1, 1, -3, 1, -1, 1, -3,
  1, -1, 3, -1, 3, -1, 3, -1, 3, -3,
  3, -1, 1, -1, 1, 0
};
static const uint32_t IDUNIT_US = 40000;     // 30 wpm
static const int8_t *idp = nullptr;
#endif

static void beepStart(int volIdx, uint32_t dur_us) {
  analogWrite(BEEP_PIN, dutyForVol[volIdx & 7]);
  toneOn = true;
  toneEnd = micros() + dur_us;
}

static void beepPoll() {
  if (toneOn && (int32_t)(micros() - toneEnd) >= 0) {
    analogWrite(BEEP_PIN, 0);
    toneOn = false;
  }
#if DOIDENT
  if (!toneOn && idp && *idp) {
    // silence gaps are handled by just waiting
    static uint32_t gapEnd = 0; static bool inGap = false;
    if (inGap) {
      if ((int32_t)(micros() - gapEnd) < 0) return;
      inGap = false;
    }
    int8_t u = *idp++;
    if (u > 0) beepStart(4, u * IDUNIT_US);
    else { inGap = true; gapEnd = micros() + (-u) * IDUNIT_US; }
  }
#endif
}

// ======================= LK201 core hooks ====================

class PicoHooks : public lk201::Hooks {
public:
  void tx(uint8_t b) override {
    Serial1.write(b);
    DBG("-> %02X\r\n", b);
  }
  void leds(uint8_t l) override {
    wantUsbLeds = (l >> 1) & 0x07;   // Compose->Num, Lock->Caps, Hold->Scroll
#ifdef WAIT_LED
    digitalWrite(WAIT_LED, (l & lk201::LED_WAIT) ? HIGH : LOW);
#endif
    DBG("LEDs %X\r\n", l);
  }
  void click(uint8_t v) override {
#if DOIDENT
    idp = nullptr;
#endif
    beepStart(v, 2500);              // 5 cycles at 2 kHz
  }
  void bell(uint8_t v) override {
#if DOIDENT
    idp = nullptr;
#endif
    beepStart(v, 125000);
  }
};
static PicoHooks hooks;
static lk201::Keyboard lk(hooks);

// ======================= Core 0 ==============================

void setup() {
#if LKDEBUG
  Serial.begin(115200);
#endif
  queue_init(&usbEvents, sizeof(UsbEvent), 32);

#ifdef WAIT_LED
  pinMode(WAIT_LED, OUTPUT);
  digitalWrite(WAIT_LED, LOW);
#endif

  // Beeper PWM.  A square wave of duty d has fundamental ~ sin(pi*d),
  // so pick d for amplitude 0.707^vol (vol 0 = loudest = 50 % duty).
  analogWriteFreq(TONE_HZ);
  analogWriteRange(PWM_RANGE);
  for (int i = 0; i < 8; i++) {
    double a = pow(0.707, i);
    dutyForVol[i] = (int)(asin(a) / M_PI * PWM_RANGE + 0.5);
  }
  analogWrite(BEEP_PIN, 0);

  Serial1.setTX(LK_TX_PIN);
  Serial1.setRX(LK_RX_PIN);
  Serial1.begin(4800, SERIAL_8N1);

#if DOIDENT
  idp = ident;
#endif

  // Announce ourselves like a real LK201 does at power-up.
  lk.begin(micros());
}

void loop() {
  while (Serial1.available()) {
    uint8_t b = Serial1.read();
    DBG("<- %02X\r\n", b);
    lk.hostByte(b, micros());
  }

  UsbEvent ev;
  while (queue_try_remove(&usbEvents, &ev)) {
    switch (ev.type) {
    case 0:
      DBG("kbd%u: %02X %02X %02X %02X %02X %02X %02X %02X\r\n", ev.src,
          ev.data[0], ev.data[1], ev.data[2], ev.data[3],
          ev.data[4], ev.data[5], ev.data[6], ev.data[7]);
      lk.hidReport(ev.src, ev.data, ev.len, micros());
      break;
    case 1:
      DBG("kbd%u mounted\r\n", ev.src);
      break;
    case 2:
      DBG("kbd%u removed\r\n", ev.src);
      lk.sourceGone(ev.src, micros());
      break;
    }
  }

  lk.poll(micros());
  beepPoll();
}

// ======================= Core 1: USB host ====================

Adafruit_USBH_Host USBHost;

struct Slot {
  bool used;
  uint8_t addr, inst;
  uint8_t ledsApplied;
  uint8_t ledBuf;            // must stay valid until the transfer is done
};
static Slot slots[lk201::Keyboard::NSRC];

static int findSlot(uint8_t addr, uint8_t inst) {
  for (int i = 0; i < lk201::Keyboard::NSRC; i++)
    if (slots[i].used && slots[i].addr == addr && slots[i].inst == inst) return i;
  return -1;
}

static void post(uint8_t type, int src, const uint8_t *d = nullptr, int len = 0) {
  UsbEvent ev = {};
  ev.type = type; ev.src = (uint8_t)src; ev.len = (uint8_t)len;
  if (d) memcpy(ev.data, d, len > 8 ? 8 : len);
  queue_try_add(&usbEvents, &ev);   // drop if full rather than stall USB
}

void setup1() {
  uint32_t cpu_hz = clock_get_hz(clk_sys);
  if (cpu_hz != 120000000UL && cpu_hz != 240000000UL) {
    // PIO-USB can't run: blink the WAIT LED fast forever.
    while (true) {
#ifdef WAIT_LED
      digitalWrite(WAIT_LED, !digitalRead(WAIT_LED));
#endif
      delay(100);
    }
  }
#ifdef PIN_5V_EN
  pinMode(PIN_5V_EN, OUTPUT);              // Feather: power the USB-A jack
  digitalWrite(PIN_5V_EN, PIN_5V_EN_STATE);
#endif
  pio_usb_configuration_t pio_cfg = PIO_USB_DEFAULT_CONFIG;
  pio_cfg.pin_dp = USB_HOST_DP;
  USBHost.configure_pio_usb(1, &pio_cfg);
  USBHost.begin(1);
}

void loop1() {
  USBHost.task();

  // Keep keyboard LEDs in step with what the DEC host asked for.
  const uint8_t want = wantUsbLeds;
  for (int i = 0; i < lk201::Keyboard::NSRC; i++) {
    Slot &s = slots[i];
    if (s.used && s.ledsApplied != want) {
      s.ledBuf = want;
      if (tuh_hid_set_report(s.addr, s.inst, 0, HID_REPORT_TYPE_OUTPUT, &s.ledBuf, 1))
        s.ledsApplied = want;
    }
  }
}

// TinyUSB host callbacks (run on core 1)

void tuh_hid_mount_cb(uint8_t addr, uint8_t inst, uint8_t const *desc, uint16_t desc_len) {
  (void)desc; (void)desc_len;
  // Only boot-protocol keyboard interfaces; mice, consumer-control and
  // vendor interfaces of combo devices are ignored.
  if (tuh_hid_interface_protocol(addr, inst) != HID_ITF_PROTOCOL_KEYBOARD) return;
  for (int i = 0; i < lk201::Keyboard::NSRC; i++) {
    if (!slots[i].used) {
      slots[i] = { true, addr, inst, 0xff, 0 };
      post(1, i);
      tuh_hid_receive_report(addr, inst);
      return;
    }
  }
}

void tuh_hid_umount_cb(uint8_t addr, uint8_t inst) {
  int i = findSlot(addr, inst);
  if (i < 0) return;
  slots[i].used = false;
  post(2, i);
}

void tuh_hid_report_received_cb(uint8_t addr, uint8_t inst, uint8_t const *rpt, uint16_t len) {
  int i = findSlot(addr, inst);
  if (i >= 0) {
    // Boot reports are 8 bytes.  A few keyboards ignore SET_PROTOCOL
    // and prefix a report ID; strip it if that's what this looks like.
    if (len == 9 && rpt[0] == 1) { rpt++; len--; }
    if (len >= 8) post(0, i, rpt, 8);
  }
  tuh_hid_receive_report(addr, inst);
}
