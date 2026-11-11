// SPDX-License-Identifier: MIT
// DHT12 (Aosong) on I2C: five registers from 0x00 - humidity integer and tenths, temperature integer and tenths (bit 7
// of the tenths: below zero), and their sum's low byte.
#include "DeviceCases.h"

namespace device_cases {
namespace dht12 {

using probe_cases::after;
using probe_cases::mark;
using probe_cases::markf;
using probe_cases::start;

namespace {

bool sumOk(const uint8_t *b) { return static_cast<uint8_t>(b[0] + b[1] + b[2] + b[3]) == b[4]; }

void result(const char *how, size_t got, const uint8_t *b) {
  char h[11];
  hex(h, b, got);
  markf("%s got=%u bytes=%s sum=%d", how, static_cast<unsigned>(got), h, got == 5 && sumOk(b));
}

// The registers read every `every_ms` for `for_ms`; an event for each new reading (the data differ from the last)
int32_t cadence(TwoWire &bus, uint8_t addr, uint32_t every_ms, uint32_t for_ms) {
  int32_t bad = 0;
  uint8_t b[5], last[5];
  char h[11], cond[32];
  size_t got = readRegs(bus, addr, 0x00, last, 5);
  snprintf(cond, sizeof cond, "read_every_ms=%lu", static_cast<unsigned long>(every_ms));
  start("update-period", cond);   // from the first read
  bad += got != 5;
  const uint32_t t0 = millis();
  uint32_t next = every_ms;
  int reads = 1, updates = 0;
  while (millis() - t0 < for_ms) {
    while (millis() - t0 < next) delay(1);
    got = readRegs(bus, addr, 0x00, b, 5);
    ++reads;
    bad += got != 5;
    if (got == 5 && memcmp(b, last, 5) != 0) {
      after("update-period", ++updates);
      memcpy(last, b, 5);
      hex(h, b, 5);
      markf("t_ms=%lu bytes=%s sum=%d", static_cast<unsigned long>(millis() - t0), h, sumOk(b));
    }
    next += every_ms;
  }
  markf("reads=%d updates=%d", reads, updates);
  return bad;
}

}  // namespace

// dht12-modes: the registers read three ways (pointer then a repeated start, pointer with a stop then a read, a read
// with no pointer), then read every 100 ms and every 1 s: how often the reading changes
int32_t modes(TwoWire &bus, uint8_t addr) {
  int32_t bad = 0;
  uint8_t b[5] = {};
  mark("# dht12-modes");
  mark("## read-repeated-start");
  size_t got = readRegs(bus, addr, 0x00, b, 5);
  result("how=repeated-start", got, b);
  bad += got != 5;
  mark("##");
  delay(100);
  mark("## read-stop");
  bus.beginTransmission(addr);
  bus.write(0x00);
  const int end = bus.endTransmission(true);
  got = readN(bus, addr, b, 5);
  markf("pointer_end=%d", end);
  result("how=stop", got, b);
  bad += got != 5;
  mark("##");
  delay(100);
  mark("## read-no-pointer");
  memset(b, 0, sizeof b);
  got = readN(bus, addr, b, 5);
  result("how=no-pointer", got, b);
  mark("##");
  delay(100);
  mark("## cadence-100ms");
  bad += cadence(bus, addr, 100, 6000);
  mark("##");
  mark("## cadence-1000ms");
  bad += cadence(bus, addr, 1000, 8000);
  mark("##");
  mark("#");
  return bad;
}

}  // namespace dht12
}  // namespace device_cases
