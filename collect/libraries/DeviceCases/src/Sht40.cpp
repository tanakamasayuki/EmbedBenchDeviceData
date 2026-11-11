// SPDX-License-Identifier: MIT
// SHT40 (Sensirion SHT4x): 8-bit commands, no clock stretching; a read is NACKed while the part works, answers come
// in words with a CRC each.
#include "DeviceCases.h"

namespace device_cases {
namespace sht40 {

using probe_cases::after;
using probe_cases::before;
using probe_cases::mark;
using probe_cases::markf;
using probe_cases::start;

namespace {

// A command, then a 6-byte read every 200 us until it answers (or `limit_us`): the event `timing` if one is given
size_t commandAndPoll(TwoWire &bus, uint8_t addr, uint8_t cmd, const char *timing, const char *cond, uint8_t *b,
                      uint32_t limit_us, int &end, int &tries) {
  end = command8(bus, addr, cmd);
  if (end == 0 && timing != nullptr) start(timing, cond);
  const uint32_t t0 = micros();
  tries = 0;
  size_t got = 0;
  while (end == 0 && micros() - t0 < limit_us) {
    ++tries;
    got = readN(bus, addr, b, 6);
    if (got == 6) {
      if (timing != nullptr) after(timing);
      break;
    }
    delayMicroseconds(200);
  }
  return got;
}

}  // namespace

// sht40-modes: soft reset (how soon a command is taken again), the serial number, a measurement at each precision,
// and the lowest heater pulse (20 mW, 0.1 s; a measurement at its end)
int32_t modes(TwoWire &bus, uint8_t addr) {
  int32_t bad = 0;
  uint8_t b[6];
  char h[13];
  int end = 0, tries = 0;
  mark("# sht40-modes");
  mark("## soft-reset");
  markf("end=%d", command8(bus, addr, 0x94));
  start("soft-reset");
  const uint32_t t0 = micros();
  int refused = 0;
  while (micros() - t0 < 5000) {   // 1 ms at the most (datasheet table 4)
    if (command8(bus, addr, 0x89) == 0) {   // the serial number command: taken once the reset is over
      after("soft-reset");
      break;
    }
    before("soft-reset");
    ++refused;
    delayMicroseconds(100);
  }
  size_t got = 0;
  for (int i = 0; i < 50 && got != 6; ++i) {
    got = readN(bus, addr, b, 6);
    if (got != 6) delayMicroseconds(200);
  }
  hex(h, b, got);
  markf("refused=%d got=%u serial=%s crc=%d", refused, static_cast<unsigned>(got), h, got == 6 && crcOk(b, 6));
  bad += got != 6;
  mark("##");
  delay(5);
  mark("## serial");
  got = commandAndPoll(bus, addr, 0x89, "serial", "", b, 10000, end, tries);
  hex(h, b, got);
  markf("end=%d tries=%d got=%u bytes=%s crc=%d", end, tries, static_cast<unsigned>(got), h, got == 6 && crcOk(b, 6));
  bad += got != 6;
  mark("##");
  delay(5);
  static const struct { const char *name; uint8_t cmd; } kPrecision[] = {
      {"high", 0xFD}, {"medium", 0xF6}, {"low", 0xE0}};
  for (const auto &p : kPrecision) {
    char timing[24];
    snprintf(timing, sizeof timing, "measure-%s", p.name);
    markf("## polling-%s", p.name);
    got = commandAndPoll(bus, addr, p.cmd, timing, "via=polling", b, 50000, end, tries);
    hex(h, b, got);
    markf("end=%d tries=%d got=%u bytes=%s crc=%d", end, tries, static_cast<unsigned>(got), h, got == 6 && crcOk(b, 6));
    bad += got != 6;
    mark("##");
    delay(5);
  }
  mark("## heater-20mw-100ms");
  got = commandAndPoll(bus, addr, 0x15, "heater-20mw-100ms", "", b, 1500000, end, tries);
  hex(h, b, got);
  markf("end=%d tries=%d got=%u bytes=%s crc=%d", end, tries, static_cast<unsigned>(got), h, got == 6 && crcOk(b, 6));
  bad += got != 6;
  mark("##");
  mark("#");
  return bad;
}

}  // namespace sht40
}  // namespace device_cases
