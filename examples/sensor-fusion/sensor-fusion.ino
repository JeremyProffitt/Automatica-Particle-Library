// sensor-fusion.ino — automatica example: sensor smoothing, calibration, and report-gating
// via SensorPipeline (SPEC §12). Demonstrates wiring the pipeline into a dual-sensor
// (temperature + humidity) Particle device that reports to Alexa.
// ============================================================================
// DEVICE ARCHETYPE: a two-channel environmental sensor node that applies
// calibration correction, EMA smoothing, and deadband gating before pushing
// each reading to the cloud. Models two endpoints in one device.
//
// CAPABILITIES SMOOTHED / CALIBRATED:
//   Channel "temp"     — temperature (tenths-of-a-degree Celsius), TemperatureSensor cap.
//                        Calibrated with a two-point lookup table to correct sensor offset.
//                        EMA alpha=0.3 for heavy smoothing; deadband=5 (0.5 degC).
//   Channel "humidity" — relative humidity (whole percent 0..100), HumiditySensor cap.
//                        Linear calibration (scale=1.0, offset=-3.0 — sensor reads 3% high).
//                        EMA alpha=0.5 moderate smoothing; deadband=2 (2% RH).
//
// HOW CALIBRATION CONFIG ARRIVES VIA LEDGER:
//   The cloud writes a retained JSON document to "automatica/<thing>/config"
//   (topic SPEC §9.5). LedgerConfig delivers it to the onConfig callback, which
//   calls pipeline.parseConfig(rawJson) to update channel configs without rebooting.
//   On boot, loadPersisted() re-applies the last received config from flash so the
//   pipeline starts calibrated even while offline.
//
//   Config JSON shape (SPEC §12.8) — paste into the Particle console or cloud CLI:
//
//   {
//     "sensorPipeline": {
//       "channels": {
//         "temp": {
//           "emaAlpha":  0.3,
//           "deadband":  5,
//           "calTable":  [
//             { "raw": 0,    "calibrated": 0   },
//             { "raw": 1000, "calibrated": 985 }
//           ]
//         },
//         "humidity": {
//           "emaAlpha":  0.5,
//           "deadband":  2,
//           "calScale":  1.0,
//           "calOffset": -3.0
//         }
//       }
//     }
//   }
//
// ALEXA UTTERANCES THIS ENABLES:
//   "Alexa, what's the temperature in the living room?"
//   "Alexa, what's the humidity in the living room?"
//
// GOTCHAS:
//   * SensorPipeline is PURE C++ with no hardware dependency — it is byte-identical
//     to the ESP32 version (particle/automatica/src/SensorPipeline.{h,cpp}).
//   * The pipeline runs on already-read raw values: call your sensor driver first,
//     then pass the raw value to pipeline.process(). The pipeline never touches GPIO.
//   * The first process() call always sets shouldReport=true to seed the cloud with
//     an initial reading on boot. Subsequent calls honour the deadband.
//   * Calibration config from LedgerConfig is applied at runtime — the sketch loops
//     with whatever config it has. If no config has ever arrived, the pipeline uses
//     the hardcoded defaults set in setup() (always safe).
//   * parseConfig() is forward-compatible: unknown JSON keys are silently ignored,
//     so a future config schema extension won't break older firmware.
//   * .ino preprocessor rule: onControl uses the fully-qualified automatica::CtlCommand
//     because the auto-prototype lands above `using namespace automatica;`.
#include "automatica.h"
#include "AutomaticaCloud.h"
#include "SensorPipeline.h"

using namespace automatica;

// ---- core + Particle cloud adapter ------------------------------------------
Automatica home;           // NB: never name it 'automatica' (namespace clash)
AutomaticaCloud cloud(home);   // wires Particle.variable/function/publish

// ---- sensor pipeline --------------------------------------------------------
SensorPipeline pipeline;

// ---- endpoint indices -------------------------------------------------------
int gTempEp;   // temperature endpoint index (set in setup, used in loop)
int gHumEp;    // humidity endpoint index

// ---- hardware stubs: replace with your sensor driver -----------------------
// readRawTempDeci() — raw temperature in tenths of a degree C (before calibration).
// e.g. DS18B20: OneWire read + scale; DHT22: .readTemperature() * 10.
static int readRawTempDeci() { return 215; }   // stub: 21.5 degC raw

// readRawHumidityPct() — raw relative humidity as whole percent (before calibration).
// e.g. DHT22: .readHumidity().
static int readRawHumidityPct() { return 58; }   // stub: 58% RH raw

// ---- control callback -------------------------------------------------------
// Both endpoints are READ-ONLY sensors — no directives are accepted.
static bool onControl(const automatica::CtlCommand& cmd, void*) {
    (void)cmd;
    return false;   // read-only sensor: any directive becomes an Alexa error
}

void setup() {
    // ---- declare endpoints + capabilities -----------------------------------
    gTempEp = home.addEndpoint("living-temp", "Living Room Temperature",
                                "automatica temperature sensor", {"TEMPERATURE_SENSOR"});
    home.addTemperatureSensor(gTempEp);
    { CapState s; s.tempDeci = readRawTempDeci(); s.scale = "CELSIUS";
      home.setInitialState(gTempEp, kCapTemperatureSensor, kNoInstance, s); }

    gHumEp = home.addEndpoint("living-humidity", "Living Room Humidity",
                               "automatica humidity sensor", {"TEMPERATURE_SENSOR"});
    home.addHumiditySensor(gHumEp);
    { CapState s; s.i = readRawHumidityPct();
      home.setInitialState(gHumEp, kCapHumidity, kNoInstance, s); }

    home.onControl(onControl);

    // ---- pipeline hardcoded defaults (overridden by LedgerConfig on ESP32) --
    // On Particle, the config blob arrives as a Particle.variable or function arg
    // rather than via LedgerConfig. Call pipeline.parseConfig(json) from your
    // cloud-function handler to update channel configs at runtime. SPEC §12.8.
    // These defaults apply before any config arrives.
    {
        ChannelConfig tempCfg;
        tempCfg.emaAlpha = 0.3f;   // heavy smoothing for a slow thermal sensor
        tempCfg.deadband = 5.0f;   // suppress sub-0.5-degC changes (5 tenths)
        CalPoint lo; lo.raw = 0.0f;    lo.calibrated = 0.0f;
        CalPoint hi; hi.raw = 1000.0f; hi.calibrated = 985.0f;
        tempCfg.calTable.push_back(lo);
        tempCfg.calTable.push_back(hi);
        pipeline.setChannel("temp", tempCfg);
    }
    {
        ChannelConfig humCfg;
        humCfg.emaAlpha  = 0.5f;
        humCfg.deadband  = 2.0f;
        humCfg.calScale  = 1.0f;
        humCfg.calOffset = -3.0f;
        pipeline.setChannel("humidity", humCfg);
    }

    home.begin();   // build the manifest + register the cloud surface. Call after all add*().
}

void loop() {
    home.loop();   // REQUIRED every loop: flushes debounced publishes + initial snapshot

    // ---- temperature --------------------------------------------------------
    {
        int raw = readRawTempDeci();
        automatica::Result r = pipeline.process("temp", static_cast<float>(raw),
                                                     static_cast<unsigned long>(millis()));
        if (r.shouldReport) {
            int calibrated = static_cast<int>(r.value);
            CapState s; s.tempDeci = calibrated; s.scale = "CELSIUS";
            home.setInitialState(gTempEp, kCapTemperatureSensor, kNoInstance, s);
            home.reportState(gTempEp);
        }
    }

    // ---- humidity -----------------------------------------------------------
    {
        int raw = readRawHumidityPct();
        automatica::Result r = pipeline.process("humidity", static_cast<float>(raw),
                                                     static_cast<unsigned long>(millis()));
        if (r.shouldReport) {
            int calibrated = static_cast<int>(r.value);
            if (calibrated < 0)   calibrated = 0;
            if (calibrated > 100) calibrated = 100;
            CapState s; s.i = calibrated;
            home.setInitialState(gHumEp, kCapHumidity, kNoInstance, s);
            home.reportState(gHumEp);
        }
    }
}
