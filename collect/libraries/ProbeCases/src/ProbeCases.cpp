// SPDX-License-Identifier: MIT
#include "ProbeCases.h"

#if defined(ARDUINO_ARCH_ESP32)

#include <OepProbeLog.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>

namespace {

oep::ProbeLog *log_ = nullptr;
portMUX_TYPE logLock = portMUX_INITIALIZER_UNLOCKED;

const probe_cases::Case *cases_ = nullptr;
size_t count_ = 0;
void (*configure_)(uint32_t) = nullptr;

enum : uint8_t { kIdle = 0, kRunning = 1, kDone = 2 };
volatile uint8_t state = kIdle, current = 0;
volatile int32_t result = 0;

const probe_cases::Case *find(uint8_t number) {
  for (size_t i = 0; i < count_; ++i) {
    if (cases_[i].number == number) return &cases_[i];
  }
  return nullptr;
}

void caseTask(void *arg) {
  result = static_cast<const probe_cases::Case *>(arg)->run();
  __atomic_thread_fence(__ATOMIC_RELEASE);
  state = kDone;
  vTaskDelete(nullptr);
}

bool start(const probe_cases::Case *c) {
  if (state == kRunning) return false;
  current = c->number;
  result = 0;
  __atomic_thread_fence(__ATOMIC_RELEASE);
  state = kRunning;
  if (xTaskCreate(caseTask, "probe_case", 8192, const_cast<probe_cases::Case *>(c), 1, nullptr) == pdPASS) return true;
  state = kIdle;
  return false;
}

class Cases final : public oep::Interface {
 public:
  static constexpr uint8_t kOpRun = 0x10, kOpConfigure = 0x14, kOpStatus = 0x20;
  explicit Cases(const char *name) : name_(name) {}
  const char *name() const override { return name_; }
  uint16_t instance() const override { return 0; }
  uint8_t revision() const override { return 1; }
  bool offers(uint8_t op) const override {
    return op == kOpRun || op == kOpStatus || (op == kOpConfigure && configure_) || oep::ProbeLog::handles(op);
  }
  bool lockFree(uint8_t op) const override { return op == kOpStatus || oep::ProbeLog::lockFree(op); }
  void setFrameLimit(size_t max_frame) override { max_read_ = max_frame > 16 ? static_cast<uint16_t>(max_frame - 16) : 0; }

  oep::Result handle(uint8_t op, const uint8_t *p, size_t n, uint8_t *out, size_t capacity) override {
    if (oep::ProbeLog::handles(op)) {
      portENTER_CRITICAL(&logLock);
      const oep::Result r = log_->handle(op, p, n, out, capacity, max_read_);
      portEXIT_CRITICAL(&logLock);
      return r;
    }
    oep::Tail tail;
    const oep::Result parsed =
        oep::plainTail(tail, p, n, op == kOpRun ? 1 : op == kOpConfigure ? 4 : 0, out, capacity);
    if (oep::refused(parsed)) return parsed;
    if (op == kOpRun) {
      const probe_cases::Case *c = find(p[0]);
      if (c == nullptr) return oep::unsupportedValue(out, capacity);
      if (!start(c)) return oep::wrongState(out, capacity);
      return oep::completed();
    }
    if (op == kOpConfigure) {
      if (state == kRunning) return oep::wrongState(out, capacity);
      configure_(oep::getU32(p));
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
  const char *name_;
  uint16_t max_read_ = 1000;
};

}  // namespace

namespace probe_cases {

void begin(oep::Endpoint &endpoint, const char *name, const Case *cases, size_t count, void (*configure)(uint32_t),
           size_t log_bytes, size_t log_marks) {
  static oep::ProbeLog log(static_cast<uint8_t *>(malloc(log_bytes)), log_bytes,
                           static_cast<oep::PositionStream::Mark *>(malloc(sizeof(oep::PositionStream::Mark) * log_marks)),
                           log_marks);
  static Cases iface(name);
  log_ = &log;
  cases_ = cases;
  count_ = count;
  configure_ = configure;
  if (!endpoint.add(iface)) {
    // A name over 48 bytes or with one label, or a second begin(): the host would not find the cases.
    log.line("ProbeCases: the interface was refused (its name?)");
  }
}

void mark(const char *text) {
  if (log_ == nullptr) return;
  portENTER_CRITICAL(&logLock);
  log_->line(text);
  portEXIT_CRITICAL(&logLock);
}

void markf(const char *format, ...) {
  char text[oep::ProbeLog::kLineMax + 1];
  va_list args;
  va_start(args, format);
  vsnprintf(text, sizeof text, format, args);
  va_end(args);
  mark(text);
}

}  // namespace probe_cases

#endif  // ARDUINO_ARCH_ESP32
