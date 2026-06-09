// door-lock.ino — automatica example
// ============================================================================
// DEVICE ARCHETYPE: a smart deadbolt / door lock, one endpoint with a single
// SINGLETON LockController.
//
// ALEXA CAPABILITY EXPOSED (singleton — addressed by capability CODE, not instance):
//   'l' Alexa.LockController — Lock / Unlock
//        - control: cmd.boolVal  (true = LOCKED, false = UNLOCKED)
//        - state:   reported as LOCKED / UNLOCKED for status queries
//
// EXACT ALEXA UTTERANCES THIS ENABLES:
//   "Alexa, lock the front door"        -> LockController Lock   (boolVal=true)
//   "Alexa, unlock the front door"      -> LockController Unlock (boolVal=false)
//   "Alexa, is the front door locked?"  -> reads back the reported lock state
//
// HARDWARE WIRING ASSUMPTION: applyLock() is a stub. Wire it to your deadbolt
// motor / solenoid so true drives the bolt to LOCKED, false to UNLOCKED.
//
// GOTCHAS:
//   - LockController is a SINGLETON: the control callback dispatches on cmd.code
//     only; cmd.instance is kNoInstance and unused.
//   - Lock is CONTROLLABLE (unlike a read-only ContactSensor): Alexa sends
//     directives here, and the device should reportState() if the bolt is also
//     moved manually so Alexa stays in sync (not shown — no manual sensor here).
//   - .ino preprocessor rule: auto-generated prototypes land ABOVE
//     'using namespace automatica;', so the callback parameter type must be
//     FULLY QUALIFIED (automatica::CtlCommand) for the generated prototype to compile.
// ============================================================================
#include "automatica.h"
#include "AutomaticaCloud.h"

using namespace automatica;

Automatica home;                 // the core facade; never name it 'automatica' (clashes with the namespace)
AutomaticaCloud cloud(home);     // Particle Device-OS adapter; its ctor calls home.setCloudPort(this)

// --- hardware stub: drive the deadbolt motor/solenoid to the requested state ---
// locked=true => bolt LOCKED, false => UNLOCKED. Fill in the body; keep the signature.
static void applyLock(bool locked) { (void)locked; }

// Control callback: invoked once per validated inbound directive. Reads:
//   cmd.code    — capability code; kCapLock ('l') for this endpoint
//   cmd.boolVal — requested lock state (true = LOCKED)
//   (cmd.instance is kNoInstance — LockController is a singleton, routed by code only)
// Return true => handled (success reported to Alexa); false => unhandled (Alexa error).
// Fully-qualify CtlCommand (the .ino preprocessor emits the prototype before 'using namespace').
static bool onControl(const automatica::CtlCommand& cmd, void*) {
    switch (cmd.code) {
        case kCapLock: applyLock(cmd.boolVal); return true;   // true = LOCKED
    }
    return false;   // unknown/unhandled -> Lambda returns an Alexa error
}

void setup() {
    // Register the endpoint. Returns the endpoint index (== Alexa endpoint idx);
    // declaration order is the stable device identity and must not change across reboots.
    //   id   "front_door"       — internal id, ^[a-z0-9_-]{1,24}$
    //   name "Front Door"       — Alexa friendlyName (what the user says)
    //   desc "automatica lock"  — Alexa description
    //   cat  {"LOCK"}           — Alexa displayCategory
    int lock = home.addEndpoint("front_door", "Front Door", "automatica lock", {"LOCK"});

    home.addLock(lock);          // 'l' Alexa.LockController (singleton; return value 0 ignored)

    // Initial state: LOCKED (reflected in the first manifest + snapshot).
    // Singleton => pass the capability CODE with instance == kNoInstance; CapState.b is the bool.
    { CapState s; s.b = true; home.setInitialState(lock, kCapLock, kNoInstance, s); }

    home.onControl(onControl);   // register the inbound-directive callback
    home.begin();                // build the manifest + register Particle var/function (call once)
}

void loop() {
    home.loop();                 // flush debounced state publishes + emit the initial snapshot
}
