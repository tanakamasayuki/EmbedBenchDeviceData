// SPDX-License-Identifier: MIT
// The collection firmware for M5Stack Unit ENV III (U001-C): the OEP probe firmware (oep-probe-arduino's OepFirmware.h,
// every default feature of the chip) with the cases of Cases.cpp on top (ProbeCases).
#include <OepFirmware.h>

void casesBegin();   // Cases.cpp: the pins, the cases' interface

void setup() {
  casesBegin();
  oep::firmware.begin();
}

void loop() { oep::firmware.loop(); }
