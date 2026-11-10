// SPDX-License-Identifier: MIT
// ProbeCases - the common part of a collection firmware: numbered cases run on an OEP probe, the probe capturing the
// same lines meanwhile, every line a case writes (mark / markf) carrying the probe's clock (OepProbeLog.h).
//
// A sketch (a copy of oep-probe-arduino's OepProbe with a file of its own beside it) gives its cases and calls begin()
// from oepUserSetup:
//
//   int32_t scan() { probe_cases::mark("# wire-scan"); ...; probe_cases::mark("#"); return 0; }
//   const probe_cases::Case kCases[] = {{1, scan}, {2, basic}};
//   void configure(uint32_t value) { bus.end(); bus.begin(kSda, kScl, value); }
//   void oepUserSetup(oep::Endpoint &endpoint, oep::PinTable &pins) {
//     probe_cases::begin(endpoint, "io.github.<you>.<name>", kCases, 2, configure);
//   }
//
// The interface (its name at most 48 bytes, two labels or more: core §13):
//   op 0x10 run        case(u8)   ->  -       starts the case on a task of its own and answers at once (busy: refused)
//   op 0x14 configure  value(u32) ->  -       passes value to the sketch's configure (not while a case runs)
//   op 0x20 status     -          ->  state(u8: 0 idle, 1 running, 2 done) case(u8) result(i32)
//   op 0x11 read, 0x12 marks, 0x13 clear      the log (OepProbeLog.h)
//
// Markers: "# <case>" opens a case and "#" closes it, "## <phase>" opens a phase and "##" closes it (wireskein's
// headings); any other line is the case's own result for the phase it is in.
#pragma once

#include <OepEndpoint.h>
#include <stddef.h>
#include <stdint.h>

namespace probe_cases {

struct Case {
  uint8_t number;
  int32_t (*run)();   // what the case returns comes back in status (0: as expected)
};

void begin(oep::Endpoint &endpoint, const char *name, const Case *cases, size_t count,
           void (*configure)(uint32_t) = nullptr, size_t log_bytes = 16384, size_t log_marks = 512);

void mark(const char *text);
void markf(const char *format, ...) __attribute__((format(printf, 1, 2)));

}  // namespace probe_cases
