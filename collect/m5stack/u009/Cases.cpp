// SPDX-License-Identifier: MIT
// Collecting cases for M5Stack Unit Color (U009: TCS34725 at 0x29), run on an OEP probe: the probe captures the same
// lines while a case runs, and every line a case writes carries the probe's clock (ProbeCases). The part's cases are
// DeviceCases'; this file holds the library case, the numbers and the bus.
//
// Cases (the heading names are the phases each case.yaml lists):
//   1 wire-scan
//   2 adafruit-tcs34725-u009-basic
//   3 tcs34725-modes
#include <OepFirmwareParts.h>
#include <OepPinTable.h>
#include <ProbeCases.h>
#include <DeviceCases.h>
#include <Wire.h>
#include <Adafruit_TCS34725.h>

namespace {

constexpr int kSda = 14, kScl = 15;   // the probe's Grove (collect/benches/p4x-grove-*.yaml)
uint32_t kHz = 100000;                // configure changes it between cases
TwoWire &bus = Wire;
constexpr uint8_t kTcs = 0x29;

using probe_cases::mark;
using probe_cases::markf;

int32_t scan() { return device_cases::wireScan(bus); }

// adafruit-tcs34725-u009-basic: the library (Adafruit TCS34725) at 24 ms and 4x: begin, then three raw readings and
// the colour temperature from the last
Adafruit_TCS34725 tcs(TCS34725_INTEGRATIONTIME_24MS, TCS34725_GAIN_4X);

int32_t basic() {
  int32_t bad = 0;
  mark("# adafruit-tcs34725-u009-basic");
  mark("## begin");
  bool ok = tcs.begin(kTcs, &bus);
  markf("ok=%d", ok);
  bad += !ok;
  mark("##");
  uint16_t r = 0, g = 0, b = 0, c = 0;
  for (int i = 0; i < 3; ++i) {
    mark("## get-raw-data");
    const uint32_t t = micros();
    tcs.getRawData(&r, &g, &b, &c);
    markf("elapsed_us=%lu r=%u g=%u b=%u c=%u", static_cast<unsigned long>(micros() - t), r, g, b, c);
    mark("##");
  }
  mark("## color-temperature");
  markf("kelvin=%u lux=%u", tcs.calculateColorTemperature_dn40(r, g, b, c), tcs.calculateLux(r, g, b));
  mark("##");
  mark("#");
  return bad;
}

const probe_cases::Case kCases[] = {
    {1, scan}, {2, basic}, {3, [] { return device_cases::tcs34725::modes(bus, kTcs); }},
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
  probe_cases::begin("io.github.tanakamasayuki.devicedata.u009-cases", kCases, sizeof kCases / sizeof kCases[0],
                     configure);
}
