// SPDX-License-Identifier: MIT
// Collecting cases for M5Stack Unit ToF (U010: VL53L0X at 0x29), run on an OEP probe: the probe captures the same
// lines while a case runs, and every line a case writes carries the probe's clock (ProbeCases).
//
// The part needs ST's long initialisation (tuning settings, SPAD and reference calibration) before it ranges, so the
// ranging case lets the Pololu library do that and then ranges by registers itself, with events.
//
// Cases (the heading names are the phases each case.yaml lists):
//   1 wire-scan
//   2 pololu-vl53l0x-u010-basic
//   3 vl53l0x-ranging
#include <OepFirmwareParts.h>
#include <OepPinTable.h>
#include <ProbeCases.h>
#include <DeviceCases.h>
#include <Wire.h>
#include <VL53L0X.h>

namespace {

constexpr int kSda = 14, kScl = 15;   // the probe's Grove (collect/benches/p4x-grove-*.yaml)
uint32_t kHz = 100000;                // configure changes it between cases
TwoWire &bus = Wire;
constexpr uint8_t kTof = 0x29;

using device_cases::hex;
using device_cases::readRegs;
using device_cases::writeReg;
using probe_cases::after;
using probe_cases::before;
using probe_cases::mark;
using probe_cases::markf;
using probe_cases::start;

int32_t scan() { return device_cases::wireScan(bus); }

VL53L0X tof;

// pololu-vl53l0x-u010-basic: the library (Pololu VL53L0X): init, three single-shot ranges, then back-to-back ranging
// for three readings
int32_t basic() {
  int32_t bad = 0;
  mark("# pololu-vl53l0x-u010-basic");
  tof.setBus(&bus);
  tof.setTimeout(500);
  mark("## init");
  uint32_t t = micros();
  bool ok = tof.init();
  markf("ok=%d elapsed_us=%lu budget_us=%lu", ok, static_cast<unsigned long>(micros() - t),
        static_cast<unsigned long>(tof.getMeasurementTimingBudget()));
  bad += !ok;
  mark("##");
  for (int i = 0; i < 3; ++i) {
    mark("## single");
    t = micros();
    const uint16_t mm = tof.readRangeSingleMillimeters();
    markf("mm=%u elapsed_us=%lu timeout=%d", mm, static_cast<unsigned long>(micros() - t), tof.timeoutOccurred());
    bad += tof.timeoutOccurred();
    mark("##");
  }
  mark("## continuous");
  tof.startContinuous(0);
  for (int i = 0; i < 3; ++i) {
    t = micros();
    const uint16_t mm = tof.readRangeContinuousMillimeters();
    markf("mm=%u elapsed_us=%lu timeout=%d", mm, static_cast<unsigned long>(micros() - t), tof.timeoutOccurred());
    bad += tof.timeoutOccurred();
  }
  tof.stopContinuous();
  mark("##");
  mark("#");
  return bad;
}

// ---- vl53l0x-ranging -----------------------------------------------------------------------------------------------
// The registers Pololu's library (after ST's API) uses: SYSRANGE_START 00 (01 single, 02 back to back), the
// interrupt status 13 (bits 2:0 non-zero once a range is there), the range 1E (16 bits), SYSTEM_INTERRUPT_CLEAR 0B;
// a single shot first writes the stop variable read at init into 91 (within 80 / FF / 00 page writes).
uint8_t stopVariable() {
  uint8_t v = 0;
  writeReg(bus, kTof, 0x80, 0x01);
  writeReg(bus, kTof, 0xFF, 0x01);
  writeReg(bus, kTof, 0x00, 0x00);
  readRegs(bus, kTof, 0x91, &v, 1);
  writeReg(bus, kTof, 0x00, 0x01);
  writeReg(bus, kTof, 0xFF, 0x00);
  writeReg(bus, kTof, 0x80, 0x00);
  return v;
}

// The interrupt status polled every 200 us until a range is there (an event of `timing`, the k-th), then the range
// read and the interrupt cleared; -1 when none came within `limit_us`
int32_t awaitRange(const char *timing, int k, uint32_t limit_us) {
  const uint32_t t0 = micros();
  uint8_t st = 0, r[2] = {};
  while (micros() - t0 < limit_us) {
    if (readRegs(bus, kTof, 0x13, &st, 1) == 1) {
      if (st & 0x07) {
        after(timing, k);
        readRegs(bus, kTof, 0x1E, r, 2);
        writeReg(bus, kTof, 0x0B, 0x01);
        return (r[0] << 8) | r[1];
      }
      if (k == 1) before(timing);
    }
    delayMicroseconds(200);
  }
  return -1;
}

// vl53l0x-ranging: the ids, init by the library, single shots at three timing budgets (20, 33, 200 ms), then back to
// back at 33 ms for ten readings
int32_t ranging() {
  int32_t bad = 0;
  uint8_t b[3];
  char h[7], cond[48];
  mark("# vl53l0x-ranging");
  mark("## id");
  const size_t got = readRegs(bus, kTof, 0xC0, b, 3);   // C0 EE, C1 AA, C2 10 (model, type, revision)
  hex(h, b, got);
  markf("got=%u c0_c2=%s", static_cast<unsigned>(got), h);
  bad += got != 3;
  mark("##");
  mark("## init");
  tof.setBus(&bus);
  tof.setTimeout(500);
  const bool ok = tof.init();
  const uint8_t stop = stopVariable();
  markf("ok=%d stop_variable=0x%02x", ok, stop);
  bad += !ok;
  mark("##");
  static const uint32_t kBudgets[] = {20000, 33000, 200000};
  for (uint32_t budget : kBudgets) {
    markf("## single-%lums", static_cast<unsigned long>(budget / 1000));
    markf("set=%d", tof.setMeasurementTimingBudget(budget));
    for (int i = 0; i < 3; ++i) {
      writeReg(bus, kTof, 0x80, 0x01);
      writeReg(bus, kTof, 0xFF, 0x01);
      writeReg(bus, kTof, 0x00, 0x00);
      writeReg(bus, kTof, 0x91, stop);
      writeReg(bus, kTof, 0x00, 0x01);
      writeReg(bus, kTof, 0xFF, 0x00);
      writeReg(bus, kTof, 0x80, 0x00);
      markf("end=%d", writeReg(bus, kTof, 0x00, 0x01));   // SYSRANGE_START: single
      snprintf(cond, sizeof cond, "budget_us=%lu mode=single", static_cast<unsigned long>(budget));
      start("ranging", cond);
      const int32_t mm = awaitRange("ranging", 1, 2 * budget + 50000);
      markf("mm=%ld", static_cast<long>(mm));
      bad += mm < 0;
      delay(10);
    }
    mark("##");
  }
  mark("## continuous-33ms");
  markf("set=%d", tof.setMeasurementTimingBudget(33000));
  tof.startContinuous(0);   // ends with SYSRANGE_START = 02
  start("ranging-period", "budget_us=33000 mode=continuous");
  int readings = 0;
  for (int k = 1; k <= 10; ++k) {
    const int32_t mm = awaitRange("ranging-period", k, 200000);
    if (mm < 0) break;
    ++readings;
    markf("mm=%ld", static_cast<long>(mm));
  }
  tof.stopContinuous();
  markf("readings=%d", readings);
  bad += readings != 10;
  mark("##");
  mark("#");
  return bad;
}

const probe_cases::Case kCases[] = {
    {1, scan},
    {2, basic},
    {3, ranging},
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
  probe_cases::begin("io.github.tanakamasayuki.devicedata.u010-cases", kCases, sizeof kCases / sizeof kCases[0],
                     configure);
}
