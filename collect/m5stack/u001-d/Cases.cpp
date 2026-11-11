// SPDX-License-Identifier: MIT
// Collecting cases for M5Stack Unit ENV IV (U001-D: SHT40 at 0x44, BMP280 at 0x76), run on an OEP probe: the probe captures the same lines while a case runs, and
// every line a case writes carries the probe's clock (ProbeCases). The parts' cases are DeviceCases'; this file holds
// the library case, the numbers and the bus.
//
// Cases (the heading names are the phases each case.yaml lists):
//   1 wire-scan
//   2 m5unit-env-u001d-basic
//   3 sht40-modes
//   4 bmp280-modes
//   5 bmp280-standby
#include <OepFirmwareParts.h>
#include <OepPinTable.h>
#include <ProbeCases.h>
#include <DeviceCases.h>
#include <Wire.h>
#include <M5UnitENV.h>

namespace {

constexpr int kSda = 14, kScl = 15;   // the probe's Grove (collect/benches/p4x-grove-*.yaml)
uint32_t kHz = 100000;                // configure changes it between cases
TwoWire &bus = Wire;
constexpr uint8_t kSht = 0x44, kBmp = 0x76;
using probe_cases::mark;
using probe_cases::markf;

int32_t scan() { return device_cases::wireScan(bus); }

// m5unit-env-u001d-basic: the library (M5Unit-ENV): begin of each part, then three rounds of update 100 ms apart
SHT4X sht4;
BMP280 bmp;

int32_t basic() {
  int32_t bad = 0;
  mark("# m5unit-env-u001d-basic");
  bool ok = false;
  mark("## sht40-begin");
  ok = sht4.begin(&bus, kSht, kSda, kScl, kHz);
  markf("ok=%d", ok);
  bad += !ok;
  mark("##");
  mark("## bmp280-begin");
  ok = bmp.begin(&bus, kBmp, kSda, kScl, kHz);
  markf("ok=%d", ok);
  bad += !ok;
  mark("##");
  for (int i = 0; i < 3; ++i) {
    uint32_t t = 0;
    mark("## sht40-update");
    t = micros();
    ok = sht4.update();
    t = micros() - t;
    markf("ok=%d elapsed_us=%lu temperature_c=%.2f humidity_rh=%.2f", ok, static_cast<unsigned long>(t), sht4.cTemp, sht4.humidity);
    bad += !ok;
    mark("##");
    mark("## bmp280-update");
    t = micros();
    ok = bmp.update();
    t = micros() - t;
    markf("ok=%d elapsed_us=%lu pressure_pa=%.1f temperature_c=%.2f", ok, static_cast<unsigned long>(t), bmp.pressure, bmp.cTemp);
    bad += !ok;
    mark("##");
    delay(100);
  }
  mark("#");
  return bad;
}

const probe_cases::Case kCases[] = {
    {1, scan}, {2, basic}, {3, [] { return device_cases::sht40::modes(bus, kSht); }},
    {4, [] { return device_cases::bmp280::modes(bus, kBmp); }}, {5, [] { return device_cases::bmp280::standby(bus, kBmp); }},
};

// On the worker (ProbeCases): the bus's interrupt is allocated on the core that begins it, the worker's
void configure(uint32_t hz) {
  if (hz < 10000 || hz > 1000000) return;
  kHz = hz;
  bus.end();
  bus.begin(kSda, kScl, kHz);
}

}  // namespace

void casesBegin() {
  oep::firmware.onSetup([](oep::Endpoint &, oep::PinTable &pins) {
    pins.claim(kSda, oep::owners::kSketch);
    pins.claim(kScl, oep::owners::kSketch);
  });
  probe_cases::begin("io.github.tanakamasayuki.devicedata.u001d-cases", kCases, sizeof kCases / sizeof kCases[0],
                     configure);
}
