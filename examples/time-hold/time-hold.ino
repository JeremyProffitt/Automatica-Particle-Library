// time-hold.ino — automatica example.
//
// DEVICE ARCHETYPE: an appliance that runs a TIMED operation which can be paused and
// resumed (e.g. a washer or an irrigation cycle), modelled as ONE endpoint.
//
// ALEXA CAPABILITIES EXPOSED (both singletons on this one endpoint):
//   'o' addPower    -> Alexa.PowerController     (CONTROLLABLE; TurnOn/TurnOff).
//        Control: cmd.boolVal (true = on, false = off). State (CapState): .b (bool).
//   'i' addTimeHold -> Alexa.TimeHoldController  (CONTROLLABLE; MOMENTARY: Hold/Resume).
//        Control: cmd.boolVal (true = Hold/pause the running operation,
//                              false = Resume/continue it).
//        MOMENTARY = it carries no persistent state, so there is NO setInitialState for it.
//        Discovery advertises configuration.allowRemoteResume = false — i.e. the device,
//        not Alexa, is responsible for actually resuming the operation.
//   displayCategory: OTHER (no dedicated Alexa category for a generic timed appliance).
//
// ALEXA UTTERANCES ENABLED:
//   "Alexa, turn on the washer"   -> PowerController TurnOn  (cmd.code=kCapPower,    boolVal=true)
//   "Alexa, turn off the washer"  -> PowerController TurnOff (cmd.code=kCapPower,    boolVal=false)
//   "Alexa, pause the washer"     -> TimeHold Hold           (cmd.code=kCapTimeHold, boolVal=true)
//   "Alexa, resume the washer"    -> TimeHold Resume         (cmd.code=kCapTimeHold, boolVal=false)
//
// HARDWARE WIRING ASSUMPTION: a power relay/triac on GPIO D7 (driven by applyPower),
// plus appliance-specific pause/resume control (applyHold). D7 is set OUTPUT in setup().
//
// GOTCHAS:
//   * MOMENTARY capability: TimeHoldController stores no state, so addTimeHold() gets NO
//     matching setInitialState() call. Only the stateful PowerController is seeded.
//   * Both capabilities are SINGLETONS: dispatched in onControl by cmd.code with
//     cmd.instance == kNoInstance — never by an instance index. The add* builders return 0.
//   * TimeHold bool POLARITY: boolVal true means Hold (pause), false means Resume —
//     the opposite intuition from "true = running", so the comment is load-bearing.
//   * .ino PROTOTYPE RULE: the Arduino preprocessor inserts onControl's auto-prototype
//     above `using namespace automatica;`, so its parameter is fully qualified as
//     automatica::CtlCommand here.
#include "automatica.h"
#include "AutomaticaCloud.h"

using namespace automatica;

Automatica home;                 // the core facade. Never name it 'automatica' (clashes with the namespace).
AutomaticaCloud cloud(home);     // ctor wires the Particle cloud surface (variable/function/publish).

// --- hardware stubs: replace with your appliance's pause/resume control ---
static void applyPower(bool on)   { digitalWrite(D7, on ? HIGH : LOW); }            // relay on D7: HIGH=on, LOW=off.
static void applyHold(bool hold)  { /* hold ? pause the cycle : resume it */ (void)hold; }

// Control callback. Signature: bool(const CtlCommand&, void* ctx); return true =
// "handled". Dispatch on cmd.code; both capabilities here are singletons so
// cmd.instance is kNoInstance and is not inspected.
// Fully-qualify CtlCommand (the .ino preprocessor emits the prototype before 'using namespace').
static bool onControl(const automatica::CtlCommand& cmd, void*) {
    switch (cmd.code) {
        case kCapPower:    applyPower(cmd.boolVal); return true; // boolVal: true=on,   false=off
        case kCapTimeHold: applyHold(cmd.boolVal);  return true; // boolVal: true=Hold, false=Resume
    }
    return false;   // unknown/unhandled -> Lambda returns an Alexa error
}

void setup() {
    pinMode(D7, OUTPUT);         // power relay pin.

    // addEndpoint(id, friendlyName, description, {displayCategories}) -> endpoint idx.
    //   id "washer" : stable device id (^[a-z0-9_-]{1,24}$); must persist across reboots.
    int washer = home.addEndpoint("washer", "Washer", "automatica time-hold appliance", {"OTHER"});

    home.addPower(washer);      // 'o' PowerController (singleton, returns 0/ignored).
    home.addTimeHold(washer);   // 'i' TimeHoldController, MOMENTARY: no setInitialState (TimeHold stores no state).

    // Seed only the stateful capability. Power starts OFF (.b = false).
    { CapState s; s.b = false; home.setInitialState(washer, kCapPower, kNoInstance, s); }

    home.onControl(onControl);  // register the control callback.
    home.begin();               // build manifest + register cloud var/function. Call once, after all add*().
}

void loop() {
    home.loop();                // REQUIRED every loop: flush debounced publishes + emit the initial snapshot.
}
