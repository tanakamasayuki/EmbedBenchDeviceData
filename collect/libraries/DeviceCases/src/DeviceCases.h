// SPDX-License-Identifier: MIT
// DeviceCases - each part's characterization cases (no part library), written once and called by the sketch of every
// product that carries the part: int32_t <part>::<case>(TwoWire &bus, uint8_t addr). A case writes its "# <case>"
// heading, its phases and events (ProbeCases.h) and returns 0 when everything answered as expected.
//
// A case's phases are the ones its case.yaml lists; its events' timing keys are the part's knowledge keys.
#pragma once

#include <Arduino.h>
#include <ProbeCases.h>
#include <Wire.h>

namespace device_cases {

void hex(char *out, const uint8_t *b, size_t n);                       // out: 2n + 1 chars
uint8_t crc8(const uint8_t *b, size_t n);                              // Sensirion: poly 0x31, init 0xFF
bool crcOk(const uint8_t *b, size_t n);                                // every word: 2 bytes and their CRC
int command16(TwoWire &bus, uint8_t addr, uint16_t cmd);               // endTransmission's answer
int command8(TwoWire &bus, uint8_t addr, uint8_t cmd);
size_t readN(TwoWire &bus, uint8_t addr, uint8_t *b, size_t n);        // the bytes got (0: refused)
int writeReg(TwoWire &bus, uint8_t addr, uint8_t reg, uint8_t value);
size_t readRegs(TwoWire &bus, uint8_t addr, uint8_t reg, uint8_t *b, size_t n);   // pointer, repeated start, read

int32_t wireScan(TwoWire &bus);                  // wire-scan: every 7-bit address, the ones that answer

namespace sht30 {
int32_t modes(TwoWire &bus, uint8_t addr);      // sht30-modes
}
namespace sht40 {
int32_t modes(TwoWire &bus, uint8_t addr);      // sht40-modes
}
namespace bmp280 {
int32_t modes(TwoWire &bus, uint8_t addr);      // bmp280-modes
int32_t standby(TwoWire &bus, uint8_t addr);    // bmp280-standby
}
namespace dht12 {
int32_t modes(TwoWire &bus, uint8_t addr);      // dht12-modes
}
namespace tcs34725 {
int32_t modes(TwoWire &bus, uint8_t addr);      // tcs34725-modes
}

}  // namespace device_cases
