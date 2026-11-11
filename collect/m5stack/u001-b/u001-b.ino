#include <OepFirmware.h>

void casesBegin();   // Cases.cpp

void setup() {
  casesBegin();
  oep::firmware.begin();
}

void loop() { oep::firmware.loop(); }
