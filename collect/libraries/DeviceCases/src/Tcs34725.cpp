// SPDX-License-Identifier: MIT
// TCS34725 (ams): a command byte (0x80 | register, 0xA0 | register to auto-increment) before every access. ENABLE 00
// (PON bit 0, AEN bit 1), ATIME 01 (integration (256 - ATIME) x 2.4 ms), CONTROL 0F (gain), ID 12 (0x44 TCS34725),
// STATUS 13 (AVALID bit 0), data C R G B from 14 (16 bits each, low byte first).
#include "DeviceCases.h"

namespace device_cases {
namespace tcs34725 {

using probe_cases::after;
using probe_cases::before;
using probe_cases::mark;
using probe_cases::markf;
using probe_cases::start;

namespace {

int put(TwoWire &bus, uint8_t addr, uint8_t reg, uint8_t value) { return writeReg(bus, addr, 0x80 | reg, value); }

size_t get(TwoWire &bus, uint8_t addr, uint8_t reg, uint8_t *b, size_t n) {
  return readRegs(bus, addr, 0xA0 | reg, b, n);
}

// Powered on, then the ADC enabled at one integration time: when the first cycle is valid (STATUS), and each new
// reading (the data differ from the last) for `cycles` cycles
int32_t integration(TwoWire &bus, uint8_t addr, uint8_t atime, int cycles) {
  int32_t bad = 0;
  uint8_t b[8], last[8], stat = 0;
  char h[17], cond[24];
  put(bus, addr, 0x00, 0x00);   // off
  delay(5);
  put(bus, addr, 0x01, atime);
  put(bus, addr, 0x0F, 0x01);   // gain 4x
  put(bus, addr, 0x00, 0x01);   // PON
  delay(3);                     // 2.4 ms before the ADC (datasheet)
  get(bus, addr, 0x14, last, 8);
  markf("end=%d", put(bus, addr, 0x00, 0x03));   // PON | AEN
  snprintf(cond, sizeof cond, "atime=0x%02X", atime);
  start("first-valid", cond);
  start("cycle-period", cond);
  const uint32_t cycle_us = (256u - atime) * 2400u;
  const uint32_t t0 = micros();
  bool valid = false;
  int readings = 0, polls = 0;
  while (micros() - t0 < (cycles + 2) * cycle_us + 10000) {
    ++polls;
    if (!valid && get(bus, addr, 0x13, &stat, 1) == 1) {
      if (stat & 0x01) {
        after("first-valid");
        valid = true;
      } else {
        before("first-valid");
      }
    }
    const size_t got = get(bus, addr, 0x14, b, 8);
    bad += got != 8;
    if (got == 8 && memcmp(b, last, 8) != 0) {
      after("cycle-period", ++readings);
      memcpy(last, b, 8);
      hex(h, b, 8);
      markf("t_us=%lu crgb=%s", static_cast<unsigned long>(micros() - t0), h);
      if (readings >= cycles) break;
    }
  }
  markf("polls=%d readings=%d valid=%d", polls, readings, valid);
  put(bus, addr, 0x00, 0x00);
  return bad + !valid;
}

}  // namespace

// tcs34725-modes: the id, the registers at power-on, and three integration times (2.4, 24, 101 ms)
int32_t modes(TwoWire &bus, uint8_t addr) {
  int32_t bad = 0;
  uint8_t b[4];
  char h[9];
  mark("# tcs34725-modes");
  mark("## id");
  size_t got = get(bus, addr, 0x12, b, 1);
  markf("got=%u id=0x%02x", static_cast<unsigned>(got), got ? b[0] : 0);
  bad += got != 1;
  mark("##");
  mark("## registers");
  got = get(bus, addr, 0x00, b, 4);   // ENABLE, ATIME, (reserved), WTIME
  hex(h, b, got);
  markf("enable_atime_x_wtime=%s", h);
  got = get(bus, addr, 0x0F, b, 1);
  markf("control=0x%02x", got ? b[0] : 0);
  mark("##");
  static const struct { const char *name; uint8_t atime; int cycles; } kTimes[] = {
      {"integration-2400us", 0xFF, 10}, {"integration-24ms", 0xF6, 6}, {"integration-101ms", 0xD6, 4}};
  for (const auto &t : kTimes) {
    markf("## %s", t.name);
    bad += integration(bus, addr, t.atime, t.cycles);
    mark("##");
  }
  mark("#");
  return bad;
}

}  // namespace tcs34725
}  // namespace device_cases
