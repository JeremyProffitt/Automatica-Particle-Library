// motion-sensor.ino — automatica example
// =============================================================================
// DEVICE ARCHETYPE: a READ-ONLY motion sensor backed by a PIR (passive infrared)
// module. One Alexa endpoint (displayCategory "MOTION_SENSOR") that REPORTS
// "motion detected" vs "clear" — it accepts no control directives.
//
// ALEXA CAPABILITY EXPOSED (SINGLETON, read-only):
//   'v'  addMotionSensor()  -> Alexa.MotionSensor
//          - State is a single bool in CapState.b: true = DETECTED, false = clear.
//          - READ-ONLY: there is no "set motion" directive. The device never
//            receives control for this capability; instead it pushes state UP to
//            Alexa with reportState() whenever the reading changes.
//
// ALEXA UTTERANCES THIS ENABLES (queries only — no commands):
//   "Alexa, is there motion in the hallway?"
//   Motion changes can also trigger Alexa Routines ("When the Hallway Motion
//   sensor detects motion, ...") because the proactive reportState() pushes the
//   DETECTED/clear state to the Alexa event gateway.
//
// HARDWARE WIRING ASSUMPTION: a PIR module's digital signal pin feeding readPir()
// (replace the stub with a digitalRead() that returns true while motion is high).
//
// GOTCHAS:
//   - Read-only sensor: this is the key contrast with a controllable device.
//     Sensors are REPORTED (setInitialState()/reportState()), not CONTROLLED.
//     The onControl handler exists only to satisfy the API and rejects every
//     directive by returning false.
//   - reportState() is coalesced and rate-limited (<=1 publish/sec per device,
//     latest-wins). Polling every loop() is fine; only an actual state CHANGE
//     calls reportState(), so we never spam the cloud.
//   - .ino prototype rule: onControl()'s signature spells out
//     `automatica::CtlCommand` because the Arduino preprocessor hoists a forward
//     prototype above the `using namespace automatica;` line.
//   - MotionSensor is a singleton: addressed by code (kCapMotionSensor) with the
//     instance arg kNoInstance, never by an instance index.
// =============================================================================
#include "automatica.h"
#include "AutomaticaCloud.h"

using namespace automatica;

Automatica home;                 // the core facade. Never name it 'automatica' (clashes with the namespace).
AutomaticaCloud cloud(home);     // Particle adapter; its ctor calls home.setCloudPort(this).

int gMotionEp;                   // endpoint idx from addEndpoint(); used to target reportState()

// --- hardware stub: read the PIR output (true = motion detected) ---
// Replace with a digitalRead() on your PIR's signal pin (and a pinMode(INPUT) in setup()).
static bool readPir() { return false; }

// Control callback. A motion sensor is READ-ONLY, so it has nothing to control:
// reject every directive by returning false (Lambda then returns an Alexa error
// for any attempted control). Signature uses fully-qualified automatica::CtlCommand
// because of the .ino prototype gotcha noted in the header block.
static bool onControl(const automatica::CtlCommand& cmd, void*) {
    (void)cmd;      // no fields are read — there are no controllable capabilities here
    return false;   // read-only sensor -> Lambda returns an Alexa error for any directive
}

void setup() {
    // addEndpoint(id, friendlyName, description, displayCategories) -> endpoint idx.
    gMotionEp = home.addEndpoint("hallway", "Hallway Motion", "automatica motion sensor", {"MOTION_SENSOR"});
    home.addMotionSensor(gMotionEp);   // 'v' MotionSensor — singleton, read-only

    // Seed the initial reported state from the current hardware reading. For a
    // singleton the instance arg is kNoInstance; CapState.b holds the bool
    // (true = motion DETECTED). This is the state Alexa sees before any change.
    { CapState s; s.b = readPir(); home.setInitialState(gMotionEp, kCapMotionSensor, kNoInstance, s); }

    home.onControl(onControl);  // register the (reject-everything) dispatcher
    home.begin();               // build the manifest + register Particle variables/function (call once)
}

void loop() {
    home.loop();   // drive the core: flush coalesced publishes + emit the initial snapshot

    // Poll the PIR; on a CHANGE, update the stored state then proactively report it.
    // We only act on transitions so reportState() (which is rate-limited to
    // <=1/sec, latest-wins) is called sparingly.
    static bool prevMotion = false;
    bool motion = readPir();
    if (motion != prevMotion) {
        prevMotion = motion;
        // Update the capability's stored state (CapState.b) to the new reading...
        { CapState s; s.b = motion; home.setInitialState(gMotionEp, kCapMotionSensor, kNoInstance, s); }
        // ...then mark it dirty so the new state is published proactively to Alexa.
        home.reportState(gMotionEp);
    }
}
