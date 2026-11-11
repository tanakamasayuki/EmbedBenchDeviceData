// SPDX-License-Identifier: MIT
// BMP280 (Bosch): registers; status F3 (bit 3 measuring, bit 0 NVM copy), CTRL_MEAS F4 (osrs_t 7:5, osrs_p 4:2,
// mode 1:0), CONFIG F5 (t_sb 7:5, filter 4:2), data F7-FC, calibration 88-9F, chip id D0 (0x58), reset E0 = B6.
#include "DeviceCases.h"

namespace device_cases {
namespace bmp280 {

using probe_cases::after;
using probe_cases::before;
using probe_cases::mark;
using probe_cases::markf;
using probe_cases::start;

namespace {

// A forced measurement written, its status polled to the end: the measuring bit rises some time after the write,
// so a clear bit counts as the end only once it was seen set
int32_t forced(TwoWire &bus, uint8_t addr, uint8_t ctrl) {
  uint8_t b[6];
  char h[13], cond[24];
  snprintf(cond, sizeof cond, "ctrl_meas=0x%02X", ctrl);
  markf("end=%d", writeReg(bus, addr, 0xF4, ctrl));
  start("conversion", cond);
  start("status-delay", cond);
  const uint32_t t0 = micros();
  int polls = 0;
  uint8_t stat = 0xff;
  bool rose = false, ended = false;
  while (micros() - t0 < 100000) {
    ++polls;
    if (readRegs(bus, addr, 0xF3, &stat, 1) == 1) {
      if (stat & 0x08) {
        if (!rose) after("status-delay");
        rose = true;
      } else if (rose) {
        after("conversion");
        ended = true;
        break;
      } else {
        before("status-delay");
      }
    }
    delayMicroseconds(100);
  }
  markf("polls=%d elapsed_us=%lu status=0x%02x", polls, static_cast<unsigned long>(micros() - t0), stat);
  const size_t got = readRegs(bus, addr, 0xF7, b, 6);
  hex(h, b, got);
  markf("got=%u data=%s", static_cast<unsigned>(got), h);
  return (got == 6 && ended) ? 0 : 1;
}

// Normal mode from sleep for `for_us`, the data read every `every_us` (0: back to back); an event for each new sample
int32_t normal(TwoWire &bus, uint8_t addr, uint8_t ctrl, uint8_t config, uint32_t every_us, uint32_t for_us) {
  int32_t bad = 0;
  uint8_t b[6], last[6];
  char h[13], cond[64];
  writeReg(bus, addr, 0xF4, 0x00);
  delay(20);   // a conversion on its way ends first: from sleep means no sample coming
  writeReg(bus, addr, 0xF5, config);
  readRegs(bus, addr, 0xF7, last, 6);   // the sample before: a new one differs from it
  markf("end=%d", writeReg(bus, addr, 0xF4, ctrl));
  snprintf(cond, sizeof cond, "ctrl_meas=0x%02X config=0x%02X from=sleep", ctrl, config);
  start("normal-period", cond);
  const uint32_t t0 = micros();
  uint32_t next = 0;
  int reads = 0, samples = 0;
  while (micros() - t0 < for_us) {
    while (micros() - t0 < next) {}
    const uint32_t at = micros() - t0;
    const size_t got = readRegs(bus, addr, 0xF7, b, 6);
    ++reads;
    bad += got != 6;
    if (got == 6 && memcmp(b, last, 6) != 0) {
      after("normal-period", ++samples);
      memcpy(last, b, 6);
      hex(h, b, 6);
      markf("t_us=%lu data=%s", static_cast<unsigned long>(at), h);
    }
    next += every_us;
  }
  markf("reads=%d samples=%d", reads, samples);
  writeReg(bus, addr, 0xF4, 0x00);
  return bad;
}

}  // namespace

// bmp280-modes: chip id, reset (until the NVM copy is over), calibration, forced measurements at three oversampling
// settings, normal mode at the shortest standby
int32_t modes(TwoWire &bus, uint8_t addr) {
  int32_t bad = 0;
  uint8_t b[26];
  char h[53];
  mark("# bmp280-modes");
  mark("## chip-id");
  size_t got = readRegs(bus, addr, 0xD0, b, 1);
  markf("got=%u chip_id=0x%02x", static_cast<unsigned>(got), got ? b[0] : 0);
  bad += got != 1;
  mark("##");
  mark("## reset");
  markf("end=%d", writeReg(bus, addr, 0xE0, 0xB6));
  start("reset");
  const uint32_t t0 = micros();
  bool busy = false, ended = false;
  int polls = 0;
  uint8_t stat = 0xff;
  while (micros() - t0 < 20000) {   // 2 ms start-up (datasheet table 2)
    ++polls;
    got = readRegs(bus, addr, 0xF3, &stat, 1);
    if (got != 1 || (stat & 0x01)) {   // refused, or the NVM still being copied
      before("reset");
      busy = true;
    } else if (busy) {   // a clear bit counts as the end only after the part was seen busy
      after("reset");
      ended = true;
      break;
    }
    delayMicroseconds(100);
  }
  markf("polls=%d elapsed_us=%lu status=0x%02x seen_busy=%d", polls, static_cast<unsigned long>(micros() - t0), stat,
        busy);
  got = readRegs(bus, addr, 0xF3, b, 3);
  hex(h, b, got);
  markf("f3_f5=%s", h);
  (void)ended;
  mark("##");
  mark("## calibration");
  got = readRegs(bus, addr, 0x88, b, 24);
  hex(h, b, got);
  markf("got=%u bytes=%s", static_cast<unsigned>(got), h);
  bad += got != 24;
  mark("##");
  writeReg(bus, addr, 0xF5, 0x00);   // no filter, standby 0.5 ms
  static const struct { const char *name; uint8_t ctrl; } kForced[] = {
      {"forced-t1-p1", 0x25}, {"forced-t1-p8", 0x31}, {"forced-t2-p16", 0x55}};
  for (const auto &f : kForced) {
    markf("## %s", f.name);
    bad += forced(bus, addr, f.ctrl);
    mark("##");
    delay(5);
  }
  mark("## normal");
  bad += normal(bus, addr, 0x27, 0x00, 0, 200000);   // t x1, p x1, standby 0.5 ms, read back to back
  mark("##");
  mark("#");
  return bad;
}

// bmp280-standby: normal mode for 2 s at three standby times, the data read every 5 ms
int32_t standby(TwoWire &bus, uint8_t addr) {
  int32_t bad = 0;
  mark("# bmp280-standby");
  static const struct { const char *name; uint8_t config; } kStandby[] = {
      {"standby-62500us", 0x20}, {"standby-125ms", 0x40}, {"standby-250ms", 0x60}};
  for (const auto &s : kStandby) {
    markf("## %s", s.name);
    bad += normal(bus, addr, 0x27, s.config, 5000, 2000000);
    mark("##");
  }
  mark("#");
  return bad;
}

}  // namespace bmp280
}  // namespace device_cases
