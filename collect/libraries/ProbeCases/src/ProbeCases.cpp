// SPDX-License-Identifier: MIT
#include "ProbeCases.h"

#include <OepFirmwareParts.h>
#include <OepProbeLog.h>
#include <OepSimpleInterface.h>
#include <OepWorker.h>
#include <stdarg.h>
#include <stdio.h>

namespace {

oep::ProbeLog *log_ = nullptr;
const probe_cases::Case *cases_ = nullptr;
size_t count_ = 0;
void (*configure_)(uint32_t) = nullptr;

const probe_cases::Case *find(uint8_t number) {
  for (size_t i = 0; i < count_; ++i) {
    if (cases_[i].number == number) return &cases_[i];
  }
  return nullptr;
}

void run(oep::Args &in, oep::Worker &w) {
  const uint8_t number = in.u8();
  const probe_cases::Case *c = find(number);
  if (c == nullptr) {
    w.fail(-1, "no such case");
    return;
  }
  const int32_t result = c->run();
  if (result != 0) w.fail(result, "the case's result");
}

oep::Result configure(oep::Args &in, oep::Reply &out) {
  const uint32_t value = in.u32();
  if (configure_ == nullptr) return out.unsupported();
  configure_(value);
  return out.done();
}

}  // namespace

namespace probe_cases {

bool begin(const char *name, const Case *cases, size_t count, void (*configure_fn)(uint32_t), size_t log_bytes,
           size_t log_marks) {
  static oep::SimpleInterface iface(name, 1);
  static oep::Worker worker;
  static oep::ProbeLog log(log_bytes, log_marks);
  cases_ = cases;
  count_ = count;
  configure_ = configure_fn;
  if (log.ok()) log_ = &log;
  bool ok = iface.addWorker(worker, 0x40);
  ok = iface.job(0x10, "run", {{"case", oep::u8}}, run) && ok;
  ok = iface.op(0x14, "configure", {{"hz", oep::u32}}, {}, configure, oep::kInWorker) && ok;
  ok = log_ != nullptr && iface.addLog(log) && ok;
  return oep::firmware.add(iface) && ok;
}

void mark(const char *text) {
  if (log_ != nullptr) log_->line(text);
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
