#include <Arduino.h>

// Fuji (软萌团子 bot) — custom firmware entry point.
// The device currently runs the prebuilt XiaoZhi AI base image in
// firmware/. Custom features will be layered here over time.

void setup() {
  Serial.begin(115200);
}

void loop() {
  // TODO: emotion/touch/motion system, voice features (see docs/).
}
