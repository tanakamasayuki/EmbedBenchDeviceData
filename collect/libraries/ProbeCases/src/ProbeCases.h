// SPDX-License-Identifier: MIT
// ProbeCases - the common part of a collection firmware: numbered cases run on an OEP probe's worker while the probe
// captures the same lines, every line a case writes (mark / markf) carrying the probe's clock (OepProbeLog.h).
//
// A sketch includes OepFirmware.h in its .ino and gives its cases from a file of its own, before oep::firmware.begin():
//
//   int32_t scan() { probe_cases::mark("# wire-scan"); ...; probe_cases::mark("#"); return 0; }
//   const probe_cases::Case kCases[] = {{1, scan}, {2, basic}};
//   void configure(uint32_t hz) { bus.end(); bus.begin(kSda, kScl, hz); }
//   void casesBegin() { probe_cases::begin("io.github.<you>.<name>", kCases, 2, configure); }   // from setup()
//
// The interface (an oep::SimpleInterface; its name at most 48 bytes, two labels or more: core §13):
//   0x10 run        case(u8)  ->  -   a job: the case runs on the worker, the op answers at once (busy: refused); a case
//                                     that returns other than 0 ends failed with that code
//   0x14 configure  hz(u32)   ->  -   the sketch's configure, run on the worker to its end
//   0x40 status, 0x41 cancel          the worker's (oep::SimpleInterface::addWorker)
//   0x11 log-read, 0x12 log-marks, 0x13 log-clear   the log (oep::SimpleInterface::addLog)
//
// The worker is a task on the core loop() does not run on (core 0 on an ESP32-P4). A peripheral with an interrupt is
// begun there - in configure or in a case - never in setup(): its interrupt is allocated on the core that begins it,
// and on loop()'s core (the endpoint, USB) an I2C master's interrupt comes late enough to answer an address NACK as a
// success.
//
// Markers: "# <case>" opens a case and "#" closes it, "## <phase>" opens a phase and "##" closes it (wireskein's
// headings); any other line is the case's own result for the phase it is in.
#pragma once

#include <stddef.h>
#include <stdint.h>

namespace probe_cases {

struct Case {
  uint8_t number;
  int32_t (*run)();   // 0: as expected; otherwise the job ends failed with this code
};

// false: the interface was refused (its name), or no memory for the log
bool begin(const char *name, const Case *cases, size_t count, void (*configure)(uint32_t) = nullptr,
           size_t log_bytes = 16384, size_t log_marks = 512);

void mark(const char *text);
void markf(const char *format, ...) __attribute__((format(printf, 1, 2)));

}  // namespace probe_cases
