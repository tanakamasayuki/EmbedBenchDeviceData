// SPDX-License-Identifier: MIT
// Collecting cases for M5Stack Unit ENV III (U001-C: SHT30 at 0x44, QMP6988 at 0x70), run on an OEP probe as
// oep-probe-arduino's extras/LibraryTest runs its cases: a file beside the firmware's .ino, the probe captures the same
// lines while a case runs, and every line the case writes (mark) carries the probe's clock.
//
//   op 0x10 run     case(u8)  ->  -        starts the case and answers at once (busy: unavailable)
//   op 0x20 status  -         ->  state(u8: 0 idle, 1 running, 2 done) case(u8) result(i32)
//   op 0x11 read, 0x12 marks, 0x13 clear   the log (OepProbeLog.h)
//
// Cases (the heading names are the phases the data repository's case.yaml lists):
//   1 scan                         every 7-bit address, the ones that answer
//   2 m5unit-env-u001c-basic       the library (M5Unit-ENV): SHT3X and QMP6988 begin, then three rounds of update
//   3 sht30-modes                  no library: soft reset, status, each repeatability by polling and by clock stretch
//   4 qmp6988-modes                no library: chip id, reset, calibration, a forced measurement, normal mode ~120 ms
#include <OepEndpoint.h>
#include <OepPinTable.h>
#include <OepProbeLog.h>
#include <M5UnitENV.h>
#include <Wire.h>

#include "OepFeatures.h"

#ifndef ENV_SDA
#define ENV_SDA 14
#endif
#ifndef ENV_SCL
#define ENV_SCL 15
#endif
#ifndef ENV_HZ
#define ENV_HZ 100000
#endif

namespace {

constexpr int kSda = ENV_SDA, kScl = ENV_SCL;
constexpr uint32_t kHz = ENV_HZ;
constexpr uint8_t kSht = 0x44, kQmp = 0x70;
TwoWire &bus = Wire;
constexpr uint8_t kOwner = oep::owners::kSketch;

// ---- the log, shared by the case's thread and the endpoint's -------------------------------------------------------
constexpr size_t kLogBytes = 16384, kLogMarks = 512;
oep::ProbeLog *log_ = nullptr;
portMUX_TYPE logLock = portMUX_INITIALIZER_UNLOCKED;
void lockLog() { portENTER_CRITICAL(&logLock); }
void unlockLog() { portEXIT_CRITICAL(&logLock); }

void mark(const char *text) {
  lockLog();
  log_->line(text);
  unlockLog();
}

void markf(const char *format, ...) __attribute__((format(printf, 1, 2)));
void markf(const char *format, ...) {
  char text[oep::ProbeLog::kLineMax + 1];
  va_list args;
  va_start(args, format);
  vsnprintf(text, sizeof text, format, args);
  va_end(args);
  mark(text);
}

void hex(char *out, const uint8_t *b, size_t n) {
  for (size_t i = 0; i < n; ++i) sprintf(out + 2 * i, "%02x", b[i]);
  out[2 * n] = 0;
}

// ---- case 1: scan --------------------------------------------------------------------------------------------------
int32_t scan() {
  mark("# scan");
  int32_t found = 0;
  for (uint8_t a = 0x01; a <= 0x7f; ++a) {
    bus.beginTransmission(a);
    if (bus.endTransmission() == 0) {
      markf("found 0x%02x", a);
      ++found;
    }
  }
  markf("found=%ld", static_cast<long>(found));
  mark("#");
  return found;
}

// ---- case 2: the library -------------------------------------------------------------------------------------------
SHT3X sht;
QMP6988 qmp;

int32_t basic() {
  int32_t bad = 0;
  mark("# m5unit-env-u001c-basic");
  mark("## sht30-begin");
  bool ok = sht.begin(&bus, kSht, kSda, kScl, kHz);
  markf("ok=%d", ok);
  bad += !ok;
  mark("##");
  mark("## qmp6988-begin");
  ok = qmp.begin(&bus, kQmp, kSda, kScl, kHz);
  markf("ok=%d", ok);
  bad += !ok;
  mark("##");
  for (int i = 0; i < 3; ++i) {
    mark("## sht30-update");
    uint32_t t = micros();
    ok = sht.update();
    t = micros() - t;
    markf("ok=%d elapsed_us=%lu temperature_c=%.2f humidity_rh=%.2f", ok, static_cast<unsigned long>(t), sht.cTemp,
          sht.humidity);
    bad += !ok;
    mark("##");
    mark("## qmp6988-update");
    t = micros();
    ok = qmp.update();
    t = micros() - t;
    markf("ok=%d elapsed_us=%lu pressure_pa=%.1f temperature_c=%.2f", ok, static_cast<unsigned long>(t), qmp.pressure,
          qmp.cTemp);
    bad += !ok;
    mark("##");
    delay(100);
  }
  mark("#");
  return bad;
}

// ---- case 3: SHT30 directly ----------------------------------------------------------------------------------------
uint8_t crc8(const uint8_t *b, size_t n) {   // poly 0x31, init 0xff (SHT3x datasheet 4.12)
  uint8_t c = 0xff;
  for (size_t i = 0; i < n; ++i) {
    c ^= b[i];
    for (int k = 0; k < 8; ++k) c = c & 0x80 ? static_cast<uint8_t>((c << 1) ^ 0x31) : static_cast<uint8_t>(c << 1);
  }
  return c;
}

int command(uint8_t addr, uint16_t cmd, bool stop = true) {
  bus.beginTransmission(addr);
  bus.write(static_cast<uint8_t>(cmd >> 8));
  bus.write(static_cast<uint8_t>(cmd));
  return bus.endTransmission(stop);
}

size_t readN(uint8_t addr, uint8_t *b, size_t n) {
  const size_t got = bus.requestFrom(addr, static_cast<uint8_t>(n));
  for (size_t i = 0; i < got; ++i) b[i] = static_cast<uint8_t>(bus.read());
  return got;
}

bool crcOk(const uint8_t *b, size_t n) {
  for (size_t i = 0; i + 2 < n + 1; i += 3)
    if (crc8(b + i, 2) != b[i + 2]) return false;
  return true;
}

int32_t shtModes() {
  int32_t bad = 0;
  uint8_t b[6];
  char h[13];
  mark("# sht30-modes");
  mark("## soft-reset");
  markf("end=%d", command(kSht, 0x30A2));
  delay(2);
  mark("##");
  mark("## status");
  int r = command(kSht, 0xF32D);
  size_t got = readN(kSht, b, 3);
  hex(h, b, got);
  markf("end=%d got=%u bytes=%s crc=%d", r, static_cast<unsigned>(got), h, got == 3 && crcOk(b, 3));
  bad += got != 3;
  mark("##");
  static const struct { const char *name; uint16_t polling, stretch; } kRep[] = {
      {"high", 0x2400, 0x2C06}, {"medium", 0x240B, 0x2C0D}, {"low", 0x2416, 0x2C10}};
  for (const auto &rep : kRep) {
    markf("## polling-%s", rep.name);
    r = command(kSht, rep.polling);
    const uint32_t t0 = micros();
    int tries = 0;
    got = 0;
    while (micros() - t0 < 50000) {   // a NACK while it measures; 15.5 ms at the most (datasheet table 4)
      ++tries;
      got = readN(kSht, b, 6);
      if (got == 6) break;
      delayMicroseconds(500);
    }
    hex(h, b, got);
    markf("end=%d tries=%d elapsed_us=%lu got=%u bytes=%s crc=%d", r, tries, static_cast<unsigned long>(micros() - t0),
          static_cast<unsigned>(got), h, got == 6 && crcOk(b, 6));
    bad += got != 6;
    mark("##");
    delay(5);
    markf("## stretch-%s", rep.name);
    r = command(kSht, rep.stretch);
    const uint32_t t1 = micros();
    got = readN(kSht, b, 6);   // the read's address ACKed, SCL held low until the result is there
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

// ---- case 4: QMP6988 directly --------------------------------------------------------------------------------------
int writeReg(uint8_t reg, uint8_t v) {
  bus.beginTransmission(kQmp);
  bus.write(reg);
  bus.write(v);
  return bus.endTransmission();
}

size_t readRegs(uint8_t reg, uint8_t *b, size_t n) {
  bus.beginTransmission(kQmp);
  bus.write(reg);
  if (bus.endTransmission(false) != 0) return 0;   // a repeated start: the part needs it (I2CDeviceDB pilot, 26 / 26)
  return readN(kQmp, b, n);
}

int32_t qmpModes() {
  int32_t bad = 0;
  uint8_t b[25];
  char h[51];
  mark("# qmp6988-modes");
  mark("## chip-id");
  size_t got = readRegs(0xD1, b, 1);
  markf("got=%u chip_id=0x%02x", static_cast<unsigned>(got), got ? b[0] : 0);
  bad += got != 1;
  mark("##");
  mark("## reset");
  markf("end=%d", writeReg(0xE0, 0xE6));
  delay(10);
  got = readRegs(0xF1, b, 1);
  markf("iir=%02x", got ? b[0] : 0xff);
  got = readRegs(0xF3, b + 1, 3);   // DEVICE_STAT, CTRL_MEAS (0xF4), IO_SETUP (0xF5)
  hex(h, b + 1, got);
  markf("f3_f5=%s", h);
  mark("##");
  mark("## calibration");
  got = readRegs(0xA0, b, 25);
  hex(h, b, got);
  markf("got=%u bytes=%s", static_cast<unsigned>(got), h);
  bad += got != 25;
  mark("##");
  mark("## forced");
  markf("end=%d", writeReg(0xF4, (1 << 5) | (4 << 2) | 1));   // temperature x1, pressure x8, forced
  const uint32_t t0 = micros();
  int polls = 0;
  uint8_t stat = 0xff;
  while (micros() - t0 < 100000) {
    ++polls;
    if (readRegs(0xF3, &stat, 1) == 1 && !(stat & 0x08)) break;   // measure bit
    delayMicroseconds(500);
  }
  markf("polls=%d elapsed_us=%lu status=0x%02x", polls, static_cast<unsigned long>(micros() - t0), stat);
  got = readRegs(0xF7, b, 6);
  hex(h, b, got);
  markf("got=%u data=%s", static_cast<unsigned>(got), h);
  bad += got != 6;
  got = readRegs(0xF4, b, 1);
  markf("ctrl_meas=0x%02x", got ? b[0] : 0);
  mark("##");
  mark("## normal");
  writeReg(0xF1, 0x02);              // IIR coefficient 4
  writeReg(0xF5, 0x00);              // standby 1 ms
  markf("end=%d", writeReg(0xF4, (1 << 5) | (4 << 2) | 3));   // normal
  const uint32_t t1 = micros();
  while (micros() - t1 < 120000) {
    got = readRegs(0xF7, b, 6);
    hex(h, b, got);
    markf("t_us=%lu got=%u data=%s", static_cast<unsigned long>(micros() - t1), static_cast<unsigned>(got), h);
    bad += got != 6;
    delay(10);
  }
  writeReg(0xF4, 0x00);   // sleep
  mark("##");
  mark("#");
  return bad;
}

// ---- the case's thread ---------------------------------------------------------------------------------------------
enum : uint8_t { kIdle = 0, kRunning = 1, kDone = 2 };
volatile uint8_t state = kIdle, current = 0;
volatile int32_t result = 0;

int32_t runCase(uint8_t which) {
  switch (which) {
    case 1: return scan();
    case 2: return basic();
    case 3: return shtModes();
    default: return qmpModes();
  }
}

void caseTask(void *) {
  result = runCase(current);
  __atomic_thread_fence(__ATOMIC_RELEASE);
  state = kDone;
  vTaskDelete(nullptr);
}

bool start(uint8_t which) {
  if (state == kRunning) return false;
  current = which;
  result = 0;
  __atomic_thread_fence(__ATOMIC_RELEASE);
  state = kRunning;
  if (xTaskCreate(caseTask, "env_case", 8192, nullptr, 1, nullptr) == pdPASS) return true;
  state = kIdle;
  return false;
}

class EnvCases final : public oep::Interface {
 public:
  static constexpr uint8_t kOpRun = 0x10, kOpStatus = 0x20;
  const char *name() const override { return "io.github.example.library-test"; }
  uint16_t instance() const override { return 0; }
  uint8_t revision() const override { return 1; }
  bool offers(uint8_t op) const override { return op == kOpRun || op == kOpStatus || oep::ProbeLog::handles(op); }
  bool lockFree(uint8_t op) const override { return op == kOpStatus || oep::ProbeLog::lockFree(op); }
  void setFrameLimit(size_t max_frame) override { max_read_ = max_frame > 16 ? static_cast<uint16_t>(max_frame - 16) : 0; }

  oep::Result handle(uint8_t op, const uint8_t *p, size_t n, uint8_t *out, size_t capacity) override {
    if (oep::ProbeLog::handles(op)) {
      lockLog();
      const oep::Result r = log_->handle(op, p, n, out, capacity, max_read_);
      unlockLog();
      return r;
    }
    oep::Tail tail;
    const oep::Result parsed = oep::plainTail(tail, p, n, op == kOpRun ? 1 : 0, out, capacity);
    if (oep::refused(parsed)) return parsed;
    if (op == kOpRun) {
      if (p[0] < 1 || p[0] > 4) return oep::unsupportedValue(out, capacity);
      if (!start(p[0])) return oep::wrongState(out, capacity);
      return oep::completed();
    }
    if (capacity < 6) return oep::failed();
    out[0] = state;
    __atomic_thread_fence(__ATOMIC_ACQUIRE);
    out[1] = current;
    oep::putU32(out + 2, static_cast<uint32_t>(result));
    return oep::completed(6);
  }

 private:
  uint16_t max_read_ = 1000;
};

EnvCases envCases;

}  // namespace

void oepUserSetup(oep::Endpoint &endpoint, oep::PinTable &pins) {
  static oep::ProbeLog log(static_cast<uint8_t *>(malloc(kLogBytes)), kLogBytes,
                           static_cast<oep::PositionStream::Mark *>(malloc(sizeof(oep::PositionStream::Mark) * kLogMarks)),
                           kLogMarks);
  log_ = &log;
  pins.claim(kSda, kOwner);
  pins.claim(kScl, kOwner);
  bus.begin(kSda, kScl, kHz);
  endpoint.add(envCases);
}
