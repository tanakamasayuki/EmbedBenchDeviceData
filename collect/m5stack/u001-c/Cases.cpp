// SPDX-License-Identifier: MIT
// Collecting cases for M5Stack Unit ENV III (U001-C: SHT30 at 0x44, QMP6988 at 0x70), run on an OEP probe: the probe
// captures the same lines while a case runs, and every line the case writes (mark) carries the probe's clock. The
// interface, the worker and the log are the ProbeCases library's; this file holds the cases and their numbers.
//
//   (ops: ProbeCases.h; configure sets the bus speed in Hz for the cases that follow, beginning the bus on the worker)
//
// Cases (the heading names are the phases the data repository's case.yaml lists):
//   1 wire-scan                    every 7-bit address, the ones that answer
//   2 m5unit-env-u001c-basic       the library (M5Unit-ENV): SHT3X and QMP6988 begin, then three rounds of update
//   3 sht30-modes                  no library: soft reset, status, each repeatability by polling and by clock stretch
//   4 qmp6988-modes                no library: chip id, reset, calibration, a forced measurement, normal mode ~120 ms
//   5 qmp6988-standby              normal mode at standby 5 ms (0x20) and 50 ms (0x40), the data read every 1 ms / 5 ms
//   6 qmp6988-measure-bit          forced measurements, each with one status read a set time after CTRL_MEAS (0-600 us)
//   7 sht30-stretch-wait           the high-repeatability clock-stretch read on the ESP-IDF driver with scl_wait_us 20000
//                                  (Wire leaves it 0: the hardware's default), then on Wire with its timeout at 1000 ms;
//                                  one polling measurement read to the end before each
//   8-10 qmp6988-standby-long      F4 0x00, 200 ms, F4 0x33 (t x1, p x8, normal), the data read every 5 ms for 2 s: standby
//                                  5 ms (8), 50 ms (9), 250 ms (10); a line only when the data changes
//   11 sht30-ready                 0x2400 then a 6-byte read every 0.5 ms for 15 ms: as the first command since power-on
//                                  (only on the case's first run after a boot), after a soft reset and 0 / 2 / 10 / 100 ms,
//                                  and after a measurement read to its end (Wire: a command after a NACKed read may not
//                                  reach the wire)
//   12 sht30-ready-idf             the same on the ESP-IDF i2c_master driver (every transfer on the wire), and after a
//                                  measurement left unread
//   13 sht30-unread-reset          a measurement left unread, then a soft reset: whether a read after it still gets the
//                                  old result (no command; then with a command), on the ESP-IDF driver
//   14 wire-absent-address         2 bytes written to 0x50, where nothing answers: the address NACK and what Wire says
//   15 esp32p4-i2c-nack-answers    100 calls to 0x50 (nothing there) for each way of calling: Wire and ESP-IDF i2c_master,
//                                  read and probe, scl_wait_us 0 / 20000, back to back or 500 us apart; what the driver
//                                  answered each time (o: success, f: failure), to set against the wire
#include <OepFirmwareParts.h>
#include <OepPinTable.h>
#include <ProbeCases.h>
#include <DeviceCases.h>
#include <M5UnitENV.h>
#include <Wire.h>
#include <driver/i2c_master.h>

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
uint32_t kHz = ENV_HZ;   // the bus speed: configure changes it between cases
constexpr uint8_t kSht = 0x44, kQmp = 0x70;
TwoWire &bus = Wire;
constexpr uint8_t kOwner = oep::owners::kSketch;

using probe_cases::after;
using probe_cases::afterRelease;
using probe_cases::before;
using probe_cases::mark;
using probe_cases::markf;
using probe_cases::start;

void hex(char *out, const uint8_t *b, size_t n) {
  for (size_t i = 0; i < n; ++i) sprintf(out + 2 * i, "%02x", b[i]);
  out[2 * n] = 0;
}

// ---- case 1: scan --------------------------------------------------------------------------------------------------
int32_t scan() { return device_cases::wireScan(bus); }

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

int32_t shtModes() { return device_cases::sht30::modes(bus, kSht); }   // DeviceCases: shared with U001-B

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
  start("conversion", "ctrl_meas=0x31");
  start("status-delay", "ctrl_meas=0x31");
  const uint32_t t0 = micros();
  int polls = 0;
  uint8_t stat = 0xff;
  bool rose = false;   // the measure bit rises some 200 us after the write: clear before that is not the end
  while (micros() - t0 < 100000) {
    ++polls;
    if (readRegs(0xF3, &stat, 1) == 1) {
      if (stat & 0x08) {   // measure bit
        if (!rose) after("status-delay");
        rose = true;
      } else if (rose) {
        after("conversion");
        break;
      } else {
        before("status-delay");
      }
    }
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
  uint8_t last[6];
  readRegs(0xF7, last, 6);           // the forced sample: a new one differs from it
  markf("end=%d", writeReg(0xF4, (1 << 5) | (4 << 2) | 3));   // normal
  start("normal-period", "ctrl_meas=0x33 io_setup=0x00 from=forced");
  const uint32_t t1 = micros();
  int samples = 0;
  while (micros() - t1 < 120000) {
    got = readRegs(0xF7, b, 6);
    if (got == 6 && memcmp(b, last, 6) != 0) {   // read every 10 ms, a sample every 11.6: none is missed
      after("normal-period", ++samples);
      memcpy(last, b, 6);
    }
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
    delay(20);              // a conversion the last phase started ends first: from sleep means no sample on its way
    writeReg(0xF1, 0x02);   // IIR 4
    writeReg(0xF5, r.io);
    uint8_t last[6];
    readRegs(0xF7, last, 6);   // the sample before: a new one differs from it
    markf("end=%d", writeReg(0xF4, (1 << 5) | (4 << 2) | 3));   // t x1, p x8, normal
    char cond[64];
    snprintf(cond, sizeof cond, "ctrl_meas=0x33 io_setup=0x%02X from=sleep", r.io);
    start("normal-period", cond);
    const uint32_t t0 = micros();
    uint32_t next = 0;
    int samples = 0;
    while (micros() - t0 < r.for_us) {
      while (micros() - t0 < next) {}
      const uint32_t at = micros() - t0;
      const size_t got = readRegs(0xF7, b, 6);
      bad += got != 6;
      if (got == 6 && memcmp(b, last, 6) != 0) {   // a line only for a new sample
        after("normal-period", ++samples);
        memcpy(last, b, 6);
        hex(h, b, got);
        markf("t_us=%lu data=%s", static_cast<unsigned long>(at), h);
      }
      next += r.every_us;
    }
    markf("samples=%d", samples);
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
    start("status-delay", "ctrl_meas=0x31");
    const uint32_t t0 = micros();
    while (micros() - t0 < d) {}
    uint8_t stat = 0xff;
    const size_t got = readRegs(0xF3, &stat, 1);
    if (got == 1) {
      if (stat & 0x08) {
        after("status-delay");
      } else {
        before("status-delay");
      }
    }
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
    if (ew == ESP_OK) start("measure-high", "via=stretch");
    t1 = micros();
    memset(b, 0, sizeof b);
    const esp_err_t er = i2c_master_receive(dev, b, 6, 100);
    if (ew == ESP_OK && er == ESP_OK) afterRelease("measure-high");
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
  if (r == 0) start("measure-high", "via=stretch");
  t1 = micros();
  got = readN(kSht, b, 6);
  if (r == 0 && got == 6) afterRelease("measure-high");
  hex(h, b, got);
  markf("end=%d elapsed_us=%lu got=%u bytes=%s crc=%d", r, static_cast<unsigned long>(micros() - t1),
        static_cast<unsigned>(got), h, got == 6 && crcOk(b, 6));
  bad += got != 6;
  bus.setTimeOut(was);
  mark("##");
  mark("#");
  return bad;
}


// ---- cases 8-10: QMP6988 normal mode for 2 s at one standby time ---------------------------------------------------
int32_t qmpStandbyLong(uint8_t io, const char *name) {
  int32_t bad = 0;
  uint8_t b[6], last[6] = {};
  char h[13];
  markf("# qmp6988-standby-long-%s", name);
  mark("## setup");
  writeReg(0xF4, 0x00);
  writeReg(0xF1, 0x02);   // IIR 4, as qmp6988-standby
  writeReg(0xF5, io);
  delay(200);
  mark("##");
  mark("## normal");
  readRegs(0xF7, last, 6);   // the sample before: a new one differs from it
  markf("end=%d", writeReg(0xF4, 0x33));   // t x1, p x8, normal
  char cond[64];
  snprintf(cond, sizeof cond, "ctrl_meas=0x33 io_setup=0x%02X from=sleep", io);
  start("normal-period", cond);
  const uint32_t t0 = micros();
  uint32_t next = 0;
  int reads = 0, changes = 0;
  while (micros() - t0 < 2000000) {
    while (micros() - t0 < next) {}
    const uint32_t at = micros() - t0;
    const size_t got = readRegs(0xF7, b, 6);
    ++reads;
    bad += got != 6;
    if (got == 6 && memcmp(b, last, 6) != 0) {   // read every 5 ms, a sample every 16 ms or more: none is missed
      after("normal-period", ++changes);
      hex(h, b, 6);
      markf("t_us=%lu data=%s", static_cast<unsigned long>(at), h);
      memcpy(last, b, 6);
    }
    next += 5000;
  }
  markf("reads=%d changes=%d", reads, changes);
  writeReg(0xF4, 0x00);
  mark("##");
  mark("#");
  return bad;
}

// ---- case 11: when SHT30 answers a polling measurement at once -----------------------------------------------------
bool booted = true;   // no command to SHT30 since this boot (only cleared by this case)

// What a 15 ms poll measures, inside its phase: a soft reset first (its wait, then the command tells whether the reset
// is over), and the measurement the command starts when its first answer is that measurement's (from: what came
// before it; null when the first answer may be an older result, or when there is no command)
struct Poll {
  const char *from = nullptr;
  bool reset = false;
  uint32_t wait_ms = 0;
  bool command = true;
};

void pollStart(const Poll &p, int r) {
  if (p.reset) {
    if (r == 0) {
      after("soft-reset");
    } else {
      before("soft-reset");
    }
  }
  if (r == 0 && p.from != nullptr) {
    char cond[48];
    snprintf(cond, sizeof cond, "via=polling from=%s", p.from);
    start("measure-high", cond);
  }
}

int32_t shtPoll15(const char *phase, const Poll &p) {
  uint8_t b[6];
  char h[13];
  markf("## %s", phase);
  if (p.reset) {
    markf("reset=%d", command(kSht, 0x30A2));
    start("soft-reset");
    delay(p.wait_ms);
  }
  const int r = command(kSht, 0x2400);
  pollStart(p, r);
  const uint32_t t0 = micros();
  uint32_t next = 0;
  int nacks = 0, answers = 0;
  while (micros() - t0 < 15000) {
    while (micros() - t0 < next) {}
    const uint32_t at = micros() - t0;
    const size_t got = readN(kSht, b, 6);
    if (got == 6) {
      if (answers == 0 && r == 0 && p.from != nullptr) after("measure-high");
      hex(h, b, 6);
      markf("t_us=%lu bytes=%s crc=%d", static_cast<unsigned long>(at), h, crcOk(b, 6));
      ++answers;
    } else {
      ++nacks;
    }
    next += 500;
  }
  markf("end=%d nacks=%d answers=%d", r, nacks, answers);
  mark("##");
  delay(20);
  return answers ? 0 : 1;
}

int32_t shtReady() {
  int32_t bad = 0;
  uint8_t b[6];
  mark("# sht30-ready");
  if (booted) {
    markf("since_boot_ms=%lu", static_cast<unsigned long>(millis()));
    bad += shtPoll15("power-on", Poll());   // nothing timed: the part may hold a result from before this boot
    booted = false;
  }
  static const uint32_t kWaits[] = {0, 2, 10, 100};
  for (uint32_t w : kWaits) {
    char name[24];
    snprintf(name, sizeof name, "reset-wait-%lums", static_cast<unsigned long>(w));
    Poll p;
    p.from = "reset";
    p.reset = true;
    p.wait_ms = w;
    bad += shtPoll15(name, p);
  }
  command(kSht, 0x2400);   // a measurement read to its end
  const uint32_t t0 = micros();
  while (micros() - t0 < 50000 && readN(kSht, b, 6) != 6) delayMicroseconds(500);
  delay(5);
  Poll p;
  p.from = "read";
  bad += shtPoll15("after-measurement", p);
  mark("#");
  return bad;
}


// ---- case 12: case 11 on the ESP-IDF driver ------------------------------------------------------------------------
i2c_master_dev_handle_t idfSht = nullptr;

int32_t idfPoll15(const char *phase, const Poll &p) {
  uint8_t b[6];
  char h[13];
  markf("## %s", phase);
  if (p.reset) {
    const uint8_t reset[2] = {0x30, 0xA2};
    markf("reset=%d", i2c_master_transmit(idfSht, reset, 2, 50));
    start("soft-reset");
    delay(p.wait_ms);
  }
  const uint8_t cmd[2] = {0x24, 0x00};
  const esp_err_t ew = p.command ? i2c_master_transmit(idfSht, cmd, 2, 50) : ESP_OK;
  if (p.command) pollStart(p, ew);
  const uint32_t t0 = micros();
  uint32_t next = 0;
  int nacks = 0, answers = 0;
  while (micros() - t0 < 15000) {
    while (micros() - t0 < next) {}
    const uint32_t at = micros() - t0;
    memset(b, 0, sizeof b);
    if (i2c_master_receive(idfSht, b, 6, 50) == ESP_OK) {
      if (answers == 0 && p.command && ew == ESP_OK && p.from != nullptr) after("measure-high");
      hex(h, b, 6);
      markf("t_us=%lu bytes=%s crc=%d", static_cast<unsigned long>(at), h, crcOk(b, 6));
      ++answers;
    } else {
      ++nacks;
    }
    next += 500;
  }
  markf("transmit=%d nacks=%d answers=%d", ew, nacks, answers);
  mark("##");
  delay(20);
  return answers ? 0 : 1;
}

// The ESP-IDF bus for SHT30 in place of Wire for one case (every transfer on the wire): fn runs between
template <typename F>
int32_t onIdf(F fn) {
  int32_t bad = 0;
  bus.end();
  i2c_master_bus_config_t bc = {};
  bc.i2c_port = I2C_NUM_0;
  bc.sda_io_num = static_cast<gpio_num_t>(kSda);
  bc.scl_io_num = static_cast<gpio_num_t>(kScl);
  bc.clk_source = I2C_CLK_SRC_DEFAULT;
  bc.glitch_ignore_cnt = 7;
  bc.flags.enable_internal_pullup = 1;
  i2c_master_bus_handle_t mb = nullptr;
  esp_err_t e = i2c_new_master_bus(&bc, &mb);
  if (e == ESP_OK) {
    i2c_device_config_t dc = {};
    dc.dev_addr_length = I2C_ADDR_BIT_LEN_7;
    dc.device_address = kSht;
    dc.scl_speed_hz = kHz;
    dc.scl_wait_us = 20000;
    e = i2c_master_bus_add_device(mb, &dc, &idfSht);
  }
  if (e != ESP_OK) {
    markf("setup=%d", e);
    ++bad;
  } else {
    bad += fn();
  }
  if (idfSht) i2c_master_bus_rm_device(idfSht);
  idfSht = nullptr;
  if (mb) i2c_del_master_bus(mb);
  bus.begin(kSda, kScl, kHz);
  return bad;
}

// ---- case 13: a result left unread across a soft reset -------------------------------------------------------------
int32_t shtUnreadReset() {
  mark("# sht30-unread-reset");
  const int32_t bad = onIdf([] {
    int32_t bad = 0;
    const uint8_t cmd[2] = {0x24, 0x00}, reset[2] = {0x30, 0xA2};
    static const uint32_t kWaits[] = {2, 10, 100};
    for (int with_command = 0; with_command < 2; ++with_command) {
      for (uint32_t w : kWaits) {
        i2c_master_transmit(idfSht, cmd, 2, 50);   // a measurement left unread
        delay(20);
        markf("reset=%d", i2c_master_transmit(idfSht, reset, 2, 50));
        delay(w);
        char name[40];
        snprintf(name, sizeof name, "%s-reset-wait-%lums", with_command ? "command" : "read-only",
                 static_cast<unsigned long>(w));
        Poll p;   // nothing timed: the first answer may be the unread result
        p.command = with_command;
        bad += idfPoll15(name, p) && with_command;
      }
    }
    Poll p;
    p.from = "read";
    bad += idfPoll15("after-measurement", p);   // a measurement read to its end, for comparison
    return bad;
  });
  mark("#");
  return bad;
}

int32_t shtReadyIdf() {
  int32_t bad = 0;
  mark("# sht30-ready-idf");
  bus.end();
  i2c_master_bus_config_t bc = {};
  bc.i2c_port = I2C_NUM_0;
  bc.sda_io_num = static_cast<gpio_num_t>(kSda);
  bc.scl_io_num = static_cast<gpio_num_t>(kScl);
  bc.clk_source = I2C_CLK_SRC_DEFAULT;
  bc.glitch_ignore_cnt = 7;
  bc.flags.enable_internal_pullup = 1;
  i2c_master_bus_handle_t mb = nullptr;
  esp_err_t e = i2c_new_master_bus(&bc, &mb);
  if (e == ESP_OK) {
    i2c_device_config_t dc = {};
    dc.dev_addr_length = I2C_ADDR_BIT_LEN_7;
    dc.device_address = kSht;
    dc.scl_speed_hz = kHz;
    dc.scl_wait_us = 20000;
    e = i2c_master_bus_add_device(mb, &dc, &idfSht);
  }
  if (e != ESP_OK) {
    markf("setup=%d", e);
    ++bad;
  } else {
    if (booted) {
      markf("since_boot_ms=%lu", static_cast<unsigned long>(millis()));
      bad += idfPoll15("power-on", Poll());   // nothing timed: the part may hold a result from before this boot
      booted = false;
    }
    static const uint32_t kWaits[] = {0, 2, 10, 100};
    for (uint32_t w : kWaits) {
      char name[24];
      snprintf(name, sizeof name, "reset-wait-%lums", static_cast<unsigned long>(w));
      Poll p;
      p.from = "reset";
      p.reset = true;
      p.wait_ms = w;
      bad += idfPoll15(name, p);
    }
    Poll read;
    read.from = "read";
    bad += idfPoll15("after-measurement", read);   // the one before was read to its end
    const uint8_t cmd[2] = {0x24, 0x00};
    i2c_master_transmit(idfSht, cmd, 2, 50);   // a measurement left unread
    delay(20);
    bad += idfPoll15("after-unread-measurement", Poll());   // nothing timed: the first answer is the unread result
    Poll none;
    none.command = false;
    bad += idfPoll15("read-only", none);       // no command: what a read gets now
  }
  if (idfSht) i2c_master_bus_rm_device(idfSht);
  idfSht = nullptr;
  if (mb) i2c_del_master_bus(mb);
  bus.begin(kSda, kScl, kHz);
  mark("#");
  return bad;
}

// ---- case 14: an address nothing answers ---------------------------------------------------------------------------
int32_t absentAddress() {
  mark("# wire-absent-address");
  mark("## write 2 bytes to 0x50");
  bus.beginTransmission(0x50);
  bus.write(0x00);
  bus.write(0x00);
  const uint8_t end = bus.endTransmission();
  markf("endTransmission=%u", end);
  mark("##");
  mark("#");
  return end == 2 ? 0 : 1;
}

// ---- case 15: what the driver answers for an address nothing answers ------------------------------------------------
constexpr uint8_t kAbsent = 0x50;
constexpr int kCalls = 100;

// The answers as lines of at most 100 characters (o: the driver said success, f: failure).
void answers(const char *outcome, int n) {
  char line[120];
  for (int i = 0; i < n; i += 100) {
    const int k = n - i < 100 ? n - i : 100;
    snprintf(line, sizeof line, "answers=%.*s", k, outcome + i);
    mark(line);
  }
}

template <typename F>
void calls(const char *phase, uint32_t gap_us, F call) {
  static char outcome[kCalls + 1];
  markf("## %s", phase);
  for (int i = 0; i < kCalls; ++i) {
    outcome[i] = call() ? 'o' : 'f';
    if (gap_us) delayMicroseconds(gap_us);
  }
  outcome[kCalls] = 0;
  answers(outcome, kCalls);
  mark("##");
  delay(20);   // a late interrupt of the last call lands here, not in the next phase
}

// A fresh Wire bus between phases: what one phase leaves in the driver is not carried into the next.
void freshWire() {
  bus.end();
  bus.begin(kSda, kScl, kHz);
}

// Wire as the application uses it, the bus begun again before each phase.
void wirePhases() {
  for (uint32_t gap : {0u, 500u}) {
    char name[40];
    snprintf(name, sizeof name, "wire-read-gap-%luus", static_cast<unsigned long>(gap));
    freshWire();
    calls(name, gap, [] { return bus.requestFrom(kAbsent, static_cast<size_t>(6)) == 6; });
    snprintf(name, sizeof name, "wire-probe-gap-%luus", static_cast<unsigned long>(gap));
    freshWire();
    calls(name, gap, [] {
      bus.beginTransmission(kAbsent);
      return bus.endTransmission() == 0;
    });
  }
}

// ESP-IDF i2c_master directly, on a bus of its own (the Wire bus ended meanwhile), its device at scl_wait_us.
void idfPhases(uint32_t scl_wait_us) {
  bus.end();
  i2c_master_bus_config_t bc = {};
  bc.i2c_port = I2C_NUM_0;
  bc.sda_io_num = static_cast<gpio_num_t>(kSda);
  bc.scl_io_num = static_cast<gpio_num_t>(kScl);
  bc.clk_source = I2C_CLK_SRC_DEFAULT;
  bc.glitch_ignore_cnt = 7;
  bc.flags.enable_internal_pullup = 1;
  static i2c_master_bus_handle_t mb;
  static i2c_master_dev_handle_t dev;
  mb = nullptr;
  dev = nullptr;
  esp_err_t e = i2c_new_master_bus(&bc, &mb);
  if (e == ESP_OK) {
    i2c_device_config_t dc = {};
    dc.dev_addr_length = I2C_ADDR_BIT_LEN_7;
    dc.device_address = kAbsent;
    dc.scl_speed_hz = kHz;
    dc.scl_wait_us = scl_wait_us;
    e = i2c_master_bus_add_device(mb, &dc, &dev);
  }
  if (e != ESP_OK) {
    markf("setup=%d", e);
  } else {
    for (uint32_t gap : {0u, 500u}) {
      char name[48];
      snprintf(name, sizeof name, "idf-read-wait-%lu-gap-%luus", static_cast<unsigned long>(scl_wait_us),
               static_cast<unsigned long>(gap));
      calls(name, gap, [] {
        static uint8_t b[6];   // static: a late receive interrupt may still write here after the call returned
        return i2c_master_receive(dev, b, 6, 50) == ESP_OK;
      });
      if (scl_wait_us == 0) {   // probe has no device: scl_wait_us does not apply to it
        snprintf(name, sizeof name, "idf-probe-gap-%luus", static_cast<unsigned long>(gap));
        calls(name, gap, [] { return i2c_master_probe(mb, kAbsent, 50) == ESP_OK; });
      }
    }
  }
  if (dev) i2c_master_bus_rm_device(dev);
  if (mb) i2c_del_master_bus(mb);
  bus.begin(kSda, kScl, kHz);
}

int32_t nackAnswers() {
  mark("# esp32p4-i2c-nack-answers");
  wirePhases();
  idfPhases(0);
  idfPhases(20000);
  mark("#");
  return 0;
}

// ---- the table ------------------------------------------------------------------------------------------------------
int32_t qmpStandbyLong5() { return qmpStandbyLong(0x20, "5ms"); }
int32_t qmpStandbyLong50() { return qmpStandbyLong(0x40, "50ms"); }
int32_t qmpStandbyLong250() { return qmpStandbyLong(0x60, "250ms"); }

const probe_cases::Case kCases[] = {
    {1, scan},           {2, basic},          {3, shtModes},          {4, qmpModes},          {5, qmpStandby},
    {6, qmpMeasureBit},  {7, shtStretchWait}, {8, qmpStandbyLong5},   {9, qmpStandbyLong50},  {10, qmpStandbyLong250},
    {11, shtReady},      {12, shtReadyIdf},   {13, shtUnreadReset},   {14, absentAddress},        {15, nackAnswers},
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
    pins.claim(kSda, kOwner);
    pins.claim(kScl, kOwner);
  });
  probe_cases::begin("io.github.tanakamasayuki.devicedata.env-cases", kCases, sizeof kCases / sizeof kCases[0],
                     configure);
}
