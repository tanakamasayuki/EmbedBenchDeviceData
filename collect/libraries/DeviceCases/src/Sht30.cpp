// SPDX-License-Identifier: MIT
// SHT30 (Sensirion SHT3x-DIS): 16-bit commands, answers in words with a CRC each.
#include "DeviceCases.h"

namespace device_cases {
namespace sht30 {

using probe_cases::after;
using probe_cases::afterRelease;
using probe_cases::mark;
using probe_cases::markf;
using probe_cases::start;

// sht30-modes: soft reset, the status register, a single-shot measurement at each repeatability by polling (a NACK
// until ready) and by clock stretching
int32_t modes(TwoWire &bus, uint8_t addr) {
  int32_t bad = 0;
  uint8_t b[6];
  char h[13];
  mark("# sht30-modes");
  mark("## soft-reset");
  markf("end=%d", command16(bus, addr, 0x30A2));
  delay(2);
  mark("##");
  mark("## status");
  int r = command16(bus, addr, 0xF32D);
  size_t got = readN(bus, addr, b, 3);
  hex(h, b, got);
  markf("end=%d got=%u bytes=%s crc=%d", r, static_cast<unsigned>(got), h, got == 3 && crcOk(b, 3));
  bad += got != 3;
  mark("##");
  static const struct { const char *name; uint16_t polling, stretch; } kRep[] = {
      {"high", 0x2400, 0x2C06}, {"medium", 0x240B, 0x2C0D}, {"low", 0x2416, 0x2C10}};
  for (const auto &rep : kRep) {
    char timing[24];
    snprintf(timing, sizeof timing, "measure-%s", rep.name);   // knowledge/sht30.yaml's timing keys
    markf("## polling-%s", rep.name);
    r = command16(bus, addr, rep.polling);
    if (r == 0) start(timing, "via=polling");
    const uint32_t t0 = micros();
    int tries = 0;
    got = 0;
    while (micros() - t0 < 50000) {   // a NACK while it measures; 15.5 ms at the most (datasheet table 4)
      ++tries;
      got = readN(bus, addr, b, 6);
      if (got == 6) {
        if (r == 0) after(timing);
        break;
      }
      delayMicroseconds(500);
    }
    hex(h, b, got);
    markf("end=%d tries=%d elapsed_us=%lu got=%u bytes=%s crc=%d", r, tries, static_cast<unsigned long>(micros() - t0),
          static_cast<unsigned>(got), h, got == 6 && crcOk(b, 6));
    bad += got != 6;
    mark("##");
    delay(5);
    markf("## stretch-%s", rep.name);
    r = command16(bus, addr, rep.stretch);
    if (r == 0) start(timing, "via=stretch");
    const uint32_t t1 = micros();
    got = readN(bus, addr, b, 6);   // the read's address ACKed, SCL held low until the result is there
    if (r == 0 && got == 6) afterRelease(timing);   // the controller gave up otherwise: no time
    hex(h, b, got);
    markf("end=%d elapsed_us=%lu got=%u bytes=%s crc=%d", r, static_cast<unsigned long>(micros() - t1),
          static_cast<unsigned>(got), h, got == 6 && crcOk(b, 6));
    bad += got != 6;
    mark("##");
    delay(5);
  }
  mark("#");
  return bad;
}

}  // namespace sht30
}  // namespace device_cases
