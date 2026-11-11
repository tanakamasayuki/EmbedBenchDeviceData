// SPDX-License-Identifier: MIT
#include "DeviceCases.h"

namespace device_cases {

void hex(char *out, const uint8_t *b, size_t n) {
  for (size_t i = 0; i < n; ++i) sprintf(out + 2 * i, "%02x", b[i]);
  out[2 * n] = 0;
}

uint8_t crc8(const uint8_t *b, size_t n) {
  uint8_t c = 0xff;
  for (size_t i = 0; i < n; ++i) {
    c ^= b[i];
    for (int k = 0; k < 8; ++k) c = c & 0x80 ? static_cast<uint8_t>((c << 1) ^ 0x31) : static_cast<uint8_t>(c << 1);
  }
  return c;
}

bool crcOk(const uint8_t *b, size_t n) {
  for (size_t i = 0; i + 3 <= n; i += 3)
    if (crc8(b + i, 2) != b[i + 2]) return false;
  return true;
}

int command16(TwoWire &bus, uint8_t addr, uint16_t cmd) {
  bus.beginTransmission(addr);
  bus.write(static_cast<uint8_t>(cmd >> 8));
  bus.write(static_cast<uint8_t>(cmd));
  return bus.endTransmission();
}

int command8(TwoWire &bus, uint8_t addr, uint8_t cmd) {
  bus.beginTransmission(addr);
  bus.write(cmd);
  return bus.endTransmission();
}

size_t readN(TwoWire &bus, uint8_t addr, uint8_t *b, size_t n) {
  const size_t got = bus.requestFrom(addr, static_cast<uint8_t>(n));
  for (size_t i = 0; i < got; ++i) b[i] = static_cast<uint8_t>(bus.read());
  return got;
}

int writeReg(TwoWire &bus, uint8_t addr, uint8_t reg, uint8_t value) {
  bus.beginTransmission(addr);
  bus.write(reg);
  bus.write(value);
  return bus.endTransmission();
}

size_t readRegs(TwoWire &bus, uint8_t addr, uint8_t reg, uint8_t *b, size_t n) {
  bus.beginTransmission(addr);
  bus.write(reg);
  if (bus.endTransmission(false) != 0) return 0;
  return readN(bus, addr, b, n);
}

int32_t wireScan(TwoWire &bus) {
  probe_cases::mark("# wire-scan");
  int32_t found = 0;
  for (uint8_t a = 0x01; a <= 0x7f; ++a) {
    bus.beginTransmission(a);
    if (bus.endTransmission() == 0) {
      probe_cases::markf("found 0x%02x", a);
      ++found;
    }
  }
  probe_cases::markf("found=%ld", static_cast<long>(found));
  probe_cases::mark("#");
  return 0;
}

}  // namespace device_cases
