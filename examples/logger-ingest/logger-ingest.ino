// logger-ingest.ino - Particle webhook bridge example for Logger.
//
// Particle devices do not publish directly to AWS IoT Core in this example.
// Configure a Particle webhook for event logger/temperature/sensor-01 that POSTs
// to /v1/logger/data with project and device injected by the webhook template.

#include "Particle.h"

SYSTEM_THREAD(ENABLED);

static unsigned long lastPublishMs = 0;

void setup() {
  Particle.connect();
}

void loop() {
  if (Particle.connected() && millis() - lastPublishMs >= 60000) {
    lastPublishMs = millis();
    Particle.publish(
      "logger/temperature/sensor-01",
      "{\"temperature\":22.5,\"humidity\":45,\"unit\":\"celsius\"}",
      PRIVATE);
  }
}
