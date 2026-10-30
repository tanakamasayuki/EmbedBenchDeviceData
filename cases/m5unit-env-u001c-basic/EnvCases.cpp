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
//   5 qmp6988-standby              normal mode at standby 5 ms (0x20) and 50 ms (0x40), the data read every 1 ms / 5 ms
//   6 qmp6988-measure-bit          forced measurements, each with one status read a set time after CTRL_MEAS (0-600 us)
//   7 sht30-stretch-wait           the high-repeatability clock-stretch read on the ESP-IDF driver with scl_wait_us 20000
//                                  (Wire leaves it 0: the hardware's default), then on Wire with its timeout at 1000 ms;
//                                  one polling measurement read to the end before each
#include <OepEndpoint.h>
#include <OepPinTable.h>
#include <OepProbeLog.h>
#include <M5UnitENV.h>
#include <Wire.h>
#include <driver/i2c_master.h>

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


// ---- case 5: QMP6988 normal mode at two standby times --------------------------------------------------------------
int32_t qmpStandby() {
  int32_t bad = 0;
  uint8_t b[6];
  char h[13];
  mark("# qmp6988-standby");
  static const struct { const char *name; uint8_t io; uint32_t every_us, for_us; } kRuns[] = {
      {"standby-5ms", 0x20, 1000, 120000}, {"standby-50ms", 0x40, 5000, 400000}};
  for (const auto &r : kRuns) {
    markf("## %s", r.name);
    writeReg(0xF4, 0x00);   // sleep before the change
    writeReg(0xF1, 0x02);   // IIR 4
    writeReg(0xF5, r.io);
    markf("end=%d", writeReg(0xF4, (1 << 5) | (4 << 2) | 3));   // t x1, p x8, normal
    const uint32_t t0 = micros();
    uint32_t next = 0;
    while (micros() - t0 < r.for_us) {
      while (micros() - t0 < next) {}
      const size_t got = readRegs(0xF7, b, 6);
      hex(h, b, got);
      markf("t_us=%lu data=%s", static_cast<unsigned long>(micros() - t0), h);
      bad += got != 6;
      next += r.every_us;
    }
    writeReg(0xF4, 0x00);
    mark("##");
  }
  mark("#");
  return bad;
}

// ---- case 6: when QMP6988's measure bit rises after a forced CTRL_MEAS ---------------------------------------------
int32_t qmpMeasureBit() {
  int32_t bad = 0;
  mark("# qmp6988-measure-bit");
  writeReg(0xF4, 0x00);
  delay(5);
  for (uint32_t d = 0; d <= 600; d += 20) {
    markf("## delay-%luus", static_cast<unsigned long>(d));
    writeReg(0xF4, (1 << 5) | (4 << 2) | 1);   // forced
    const uint32_t t0 = micros();
    while (micros() - t0 < d) {}
    uint8_t stat = 0xff;
    const size_t got = readRegs(0xF3, &stat, 1);
    markf("after_us=%lu status=0x%02x", static_cast<unsigned long>(micros() - t0), stat);
    bad += got != 1;
    delay(15);   // the measurement ends (about 10.8 ms at t x1, p x8)
    mark("##");
  }
  mark("#");
  return bad;
}

// ---- case 7: SHT30 high repeatability, clock stretch, with longer waits --------------------------------------------
// One high-repeatability measurement by polling, read to its last byte: the state sht30-modes leaves before a stretch
// read (right after a soft reset the part answered a stretch read at once, without holding SCL)
void primeSht() {
  uint8_t b[6];
  command(kSht, 0x2400);
  const uint32_t t0 = micros();
  while (micros() - t0 < 50000 && readN(kSht, b, 6) != 6) delayMicroseconds(500);
  delay(5);
}

int32_t shtStretchWait() {
  int32_t bad = 0;
  uint8_t b[6];
  char h[13];
  mark("# sht30-stretch-wait");
  int r = 0;
  uint32_t t1 = 0;
  size_t got = 0;
  primeSht();   // one polling measurement read to the end first, as in sht30-modes before its stretch reads
  mark("## idf-scl-wait-20000us");
  bus.end();
  i2c_master_bus_config_t bc = {};
  bc.i2c_port = I2C_NUM_0;
  bc.sda_io_num = static_cast<gpio_num_t>(kSda);
  bc.scl_io_num = static_cast<gpio_num_t>(kScl);
  bc.clk_source = I2C_CLK_SRC_DEFAULT;
  bc.glitch_ignore_cnt = 7;
  bc.flags.enable_internal_pullup = 1;   // as Wire sets it
  i2c_master_bus_handle_t mb = nullptr;
  i2c_master_dev_handle_t dev = nullptr;
  esp_err_t e = i2c_new_master_bus(&bc, &mb);
  if (e == ESP_OK) {
    i2c_device_config_t dc = {};
    dc.dev_addr_length = I2C_ADDR_BIT_LEN_7;
    dc.device_address = kSht;
    dc.scl_speed_hz = kHz;
    dc.scl_wait_us = 20000;
    e = i2c_master_bus_add_device(mb, &dc, &dev);
  }
  if (e == ESP_OK) {
    const uint8_t cmd[2] = {0x2C, 0x06};
    const esp_err_t ew = i2c_master_transmit(dev, cmd, 2, 100);
    t1 = micros();
    memset(b, 0, sizeof b);
    const esp_err_t er = i2c_master_receive(dev, b, 6, 100);
    hex(h, b, 6);
    markf("transmit=%d receive=%d elapsed_us=%lu bytes=%s crc=%d", ew, er, static_cast<unsigned long>(micros() - t1), h,
          er == ESP_OK && crcOk(b, 6));
    bad += er != ESP_OK;
  } else {
    markf("setup=%d", e);
    ++bad;
  }
  if (dev) i2c_master_bus_rm_device(dev);
  if (mb) i2c_del_master_bus(mb);
  bus.begin(kSda, kScl, kHz);
  mark("##");
  delay(20);
  primeSht();   // one polling measurement read to the end first, as in sht30-modes before its stretch reads
  mark("## wire-timeout-1000ms");
  const uint16_t was = bus.getTimeOut();
  bus.setTimeOut(1000);
  r = command(kSht, 0x2C06);
  t1 = micros();
  got = readN(kSht, b, 6);
  hex(h, b, got);
  markf("end=%d elapsed_us=%lu got=%u bytes=%s crc=%d", r, static_cast<unsigned long>(micros() - t1),
        static_cast<unsigned>(got), h, got == 6 && crcOk(b, 6));
  bad += got != 6;
  bus.setTimeOut(was);
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
    case 4: return qmpModes();
    case 5: return qmpStandby();
    case 6: return qmpMeasureBit();
    default: return shtStretchWait();
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
      if (p[0] < 1 || p[0] > 7) return oep::unsupportedValue(out, capacity);
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
