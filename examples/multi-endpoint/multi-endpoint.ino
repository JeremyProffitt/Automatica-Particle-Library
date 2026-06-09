// multi-endpoint.ino — automatica example
// =============================================================================
// DEVICE ARCHETYPE: ONE microcontroller exposing THREE distinct Alexa endpoints
// from a single sketch — a 2-gang wall switch (two independent "SWITCH" devices)
// plus a read-only "TEMPERATURE_SENSOR" device. Each endpoint shows up in the
// Alexa app as its own device with its own name.
//
// ALEXA CAPABILITIES EXPOSED (all SINGLETON; one capability per endpoint):
//   endpoint gLeftEp  "Left Switch"      'o' addPower() -> Alexa.PowerController (TurnOn/TurnOff, cmd.boolVal)
//   endpoint gRightEp "Right Switch"     'o' addPower() -> Alexa.PowerController (TurnOn/TurnOff, cmd.boolVal)
//   endpoint gTempEp  "Room Temperature" 'e' addTemperatureSensor() -> Alexa.TemperatureSensor (READ-ONLY)
//          - Temperature state lives in CapState.tempDeci (tenths of a degree;
//            e.g. 72.5 deg = 725) plus CapState.scale ("CELSIUS"|"FAHRENHEIT"|"KELVIN").
//          - Read-only: REPORTED via reportState(), never controlled.
//
// ALEXA UTTERANCES THIS ENABLES:
//   "Alexa, turn on the Left Switch"     -> PowerController on gLeftEp,  cmd.boolVal=true
//   "Alexa, turn off the Right Switch"   -> PowerController on gRightEp, cmd.boolVal=false
//   "Alexa, what's the Room Temperature?"-> reads the reported TemperatureSensor state
//
// HARDWARE WIRING ASSUMPTION: two relays (left/right) driven from applyLeftPower()
// / applyRightPower(), and a temperature probe read by readTempDeciF().
//
// KEY CONCEPT — ROUTING BY ENDPOINT (cmd.idx):
//   A single onControl() handles directives for ALL endpoints. cmd.code tells you
//   the capability; cmd.idx tells you WHICH endpoint was targeted. Because both
//   switches use the SAME singleton PowerController code ('o'), the code alone is
//   ambiguous — you MUST disambiguate on cmd.idx (compare against the saved
//   gLeftEp / gRightEp indices). This is why we capture each addEndpoint() return.
//
// GOTCHAS:
//   - Singleton-code collision across endpoints: route on cmd.idx, not just
//     cmd.code, whenever two endpoints share a singleton capability.
//   - Read-only sensor: the temperature endpoint has NO control case; it is only
//     ever reported (setInitialState() + reportState()).
//   - .ino prototype rule: onControl()'s signature spells out
//     `automatica::CtlCommand` because the Arduino preprocessor hoists a forward
//     prototype above the `using namespace automatica;` line.
//   - Endpoint ORDER is the device identity and must stay stable across reboots —
//     keep the addEndpoint() calls in the same sequence (idx 0=left, 1=right, 2=temp).
// =============================================================================
#include "automatica.h"
#include "AutomaticaCloud.h"

using namespace automatica;

Automatica home;                 // the core facade. Never name it 'automatica' (clashes with the namespace).
AutomaticaCloud cloud(home);     // Particle adapter; its ctor calls home.setCloudPort(this).

// Endpoint indices returned by addEndpoint(). Saved at global scope so the control
// callback can route on cmd.idx and loop() can target reportState().
int gLeftEp, gRightEp, gTempEp;

// --- hardware stubs: replace with your relay pins + temperature probe ---
static void applyLeftPower(bool on)  { /* digitalWrite(relayL, on ? HIGH : LOW) */ (void)on; }
static void applyRightPower(bool on) { /* digitalWrite(relayR, on ? HIGH : LOW) */ (void)on; }
// Read the probe in tenths of a degree Fahrenheit (e.g. 72.5F -> 725):
static int readTempDeciF() { return 725; }

// Control callback: invoked once per validated directive across ALL endpoints.
// Return true = handled (success); false = unhandled (Lambda returns an Alexa
// error). Signature uses fully-qualified automatica::CtlCommand per the .ino
// prototype gotcha in the header block.
static bool onControl(const automatica::CtlCommand& cmd, void*) {
    switch (cmd.code) {
        case kCapPower:                          // 'o' — TurnOn/TurnOff, value in cmd.boolVal
            // Both switches share this singleton code, so route by WHICH endpoint
            // (cmd.idx) was targeted — compare against the saved endpoint indices.
            if (cmd.idx == gLeftEp)  { applyLeftPower(cmd.boolVal);  return true; }
            if (cmd.idx == gRightEp) { applyRightPower(cmd.boolVal); return true; }
            break;
        // The temperature sensor ('e') is read-only: no control case exists for it.
    }
    return false;   // unknown/unhandled -> Lambda returns an Alexa error
}

void setup() {
    // addEndpoint(id, friendlyName, description, displayCategories) -> endpoint idx.
    // Capture each returned idx; the order here (0,1,2) is the stable device identity.
    gLeftEp  = home.addEndpoint("switch_left",  "Left Switch",  "automatica wall switch", {"SWITCH"});
    gRightEp = home.addEndpoint("switch_right", "Right Switch", "automatica wall switch", {"SWITCH"});
    gTempEp  = home.addEndpoint("room_temp",    "Room Temperature", "automatica temperature sensor", {"TEMPERATURE_SENSOR"});

    home.addPower(gLeftEp);             // 'o' PowerController on the left switch (singleton)
    home.addPower(gRightEp);            // 'o' PowerController on the right switch (singleton)
    home.addTemperatureSensor(gTempEp);// 'e' TemperatureSensor on the temp endpoint (singleton, read-only)

    // Initial state for each endpoint (singletons -> instance arg is kNoInstance).
    { CapState s; s.b = false; home.setInitialState(gLeftEp,  kCapPower, kNoInstance, s); }   // CapState.b = power off
    { CapState s; s.b = false; home.setInitialState(gRightEp, kCapPower, kNoInstance, s); }   // CapState.b = power off
    // Temperature seed: CapState.tempDeci = tenths of a degree, CapState.scale = unit string.
    { CapState s; s.tempDeci = readTempDeciF(); s.scale = "FAHRENHEIT"; home.setInitialState(gTempEp, kCapTemperatureSensor, kNoInstance, s); }

    home.onControl(onControl);  // register the dispatcher above
    home.begin();               // build the manifest + register Particle variables/function (call once)
}

void loop() {
    home.loop();   // drive the core: flush coalesced publishes + emit the initial snapshot

    // Read-only sensor: poll the probe, and on a CHANGE push a fresh state report.
    // reportState() is rate-limited (<=1/sec, latest-wins), so reporting only on
    // a transition keeps publishing minimal.
    static int sPrevTempDeci = -100000;             // sentinel: forces a report on the first read
    int tempDeci = readTempDeciF();
    if (tempDeci != sPrevTempDeci) {
        sPrevTempDeci = tempDeci;
        // Update the sensor's stored state to the new reading (tenths of a degree + scale)...
        { CapState s; s.tempDeci = tempDeci; s.scale = "FAHRENHEIT"; home.setInitialState(gTempEp, kCapTemperatureSensor, kNoInstance, s); }
        // ...then mark it dirty so the new temperature is published proactively to Alexa.
        home.reportState(gTempEp);
    }
}
