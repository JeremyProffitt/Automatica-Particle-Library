// humidity-sensor.ino — automatica example
// =============================================================================
// DEVICE ARCHETYPE: a READ-ONLY relative-humidity sensor that reports ambient
// humidity as a whole percentage (0..100). It is REPORTED, never controlled.
//
// ALEXA CAPABILITIES EXPOSED
//   'n' addHumiditySensor (SINGLETON, read-only):
//        - The device packs a single int percent in CapState.i (0..100; e.g.
//          55 means 55% RH). No units field is needed on-device.
//        - The Lambda renders 'n' as an Alexa.RangeController INSTANCE named
//          "Humidity" with supportedRange 0..100, unitOfMeasure Percent, and
//          retrievable = true (so Alexa can query the last reported value).
//        - There is NO inbound control for this capability — the device only
//          REPORTS state via setInitialState() + reportState(). See Gotchas.
//
// ALEXA UTTERANCES THIS ENABLES
//   "Alexa, what's the humidity in the greenhouse?"   -> read-back of reported state
//   (No actionable utterances: a read-only sensor accepts no directives.)
//
// HARDWARE WIRING ASSUMPTION
//   readHumidityPct() is a stub returning 0. Replace with your DHT22 / SHT31 /
//   etc. driver returning whole-percent RH. No GPIO is touched here.
//
// GOTCHAS
//   - READ-ONLY SENSOR PATTERN: sensors are REPORTED, not controlled. You push
//     the latest reading with setInitialState() (which sets the CapState) and
//     then call reportState(idx) to mark it dirty; loop() coalesces and publishes
//     it (latest-wins, ≤1 publish/sec — kPublishMinIntervalMs). The onControl
//     handler returns false for everything because no directive is valid.
//   - 'n' is a SINGLETON: addressed by code (kCapHumidity) with instance ==
//     kNoInstance, never by an instance index.
//   - Facade object is named `home`, not `automatica`, to avoid clashing with
//     `namespace automatica`.
//   - The onControl signature fully-qualifies automatica::CtlCommand for the
//     .ino-generated forward prototype the preprocessor inserts above
//     `using namespace automatica;`.
// =============================================================================
#include "automatica.h"
#include "AutomaticaCloud.h"

using namespace automatica;

Automatica home;                 // facade; never name it 'automatica' (namespace clash)
AutomaticaCloud cloud(home);     // ctor wires Particle.variable/function/publish to the facade

int gHumEp;                      // endpoint index of the humidity sensor (set in setup)

// --- hardware stub: read the relative humidity as a whole percent (0..100) ---
// e.g. a return of 55 means 55% RH. Replace with your DHT22/SHT31/etc. driver.
static int readHumidityPct() { return 0; }

// Humidity sensors have NO control: every directive is rejected. (A read-only
// capability never appears as a controllable target, so this handler should never
// be invoked for it; returning false is the safe default.) Param type is
// fully-qualified for the .ino-generated prototype (see Gotchas).
static bool onControl(const automatica::CtlCommand& cmd, void*) {
    (void)cmd;
    return false;   // read-only sensor -> Lambda returns an Alexa error for any directive
}

void setup() {
    // addEndpoint(id, friendlyName, description, displayCategories) -> Alexa idx.
    //   "greenhouse" must match ^[a-z0-9_-]{1,24}$ and stay stable across reboots.
    //   {"TEMPERATURE_SENSOR"} is the closest Alexa display category for an
    //   ambient environmental sensor.
    gHumEp = home.addEndpoint("greenhouse", "Greenhouse Humidity", "automatica humidity sensor", {"TEMPERATURE_SENSOR"});
    home.addHumiditySensor(gHumEp);   // 'n' read-only humidity singleton (no instance index used)

    // Seed the initial reported state from the current hardware reading. For the
    // 'n' singleton pass kNoInstance; CapState.i carries the whole percent (0..100).
    { CapState s; s.i = readHumidityPct(); home.setInitialState(gHumEp, kCapHumidity, kNoInstance, s); }

    home.onControl(onControl); // register the (reject-all) callback
    home.begin();              // build manifest + register cloud variable/function — call once
}

void loop() {
    home.loop();   // flush debounced state publishes (≤1/sec) + emit the initial snapshot

    // Poll the humidity; report only when it changes by >= 2 percentage points to
    // avoid spamming the cloud with sensor jitter (publishes are still capped at
    // ≤1/sec by the facade, but edge-detection avoids needless dirty-marking).
    static int prevPct = readHumidityPct();
    int pct = readHumidityPct();
    int delta = pct - prevPct;
    if (delta < 0) delta = -delta;
    if (delta >= 2) {
        prevPct = pct;
        // Update the reported CapState, then mark it dirty. setInitialState here is
        // simply the API for writing the current CapState; reportState() queues the
        // coalesced publish that loop() will flush.
        { CapState s; s.i = pct; home.setInitialState(gHumEp, kCapHumidity, kNoInstance, s); }
        home.reportState(gHumEp);
    }
}
