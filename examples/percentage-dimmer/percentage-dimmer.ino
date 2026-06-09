// percentage-dimmer.ino — automatica example
// =============================================================================
// DEVICE ARCHETYPE: a generic percentage dimmer — one Alexa endpoint
// (displayCategory "LIGHT") combining on/off power with a 0..100 percent level.
//
// ALEXA CAPABILITIES EXPOSED (both SINGLETON — one per endpoint, addressed in the
// callback by cmd.code, never by an instance index):
//   'o'  addPower()      -> Alexa.PowerController
//          - TurnOn / TurnOff. Value arrives in cmd.boolVal (true = on).
//          - State (CapState.b): true = on, false = off.
//   'p'  addPercentage() -> Alexa.PercentageController
//          - A plain 0..100 percent level in cmd.intVal (integer, unit = percent).
//          - State (CapState.i): the current level 0..100.
//          - NOTE: PercentageController is DISTINCT from BrightnessController
//            ('b'). Use PercentageController for dimmers/loads Alexa addresses by
//            "percent"; use BrightnessController for things Alexa addresses by
//            "brightness". This example deliberately demonstrates 'p'.
//
// ALEXA UTTERANCES THIS ENABLES:
//   "Alexa, turn on the Hallway Dimmer"       -> PowerController, cmd.boolVal=true
//   "Alexa, turn off the Hallway Dimmer"      -> PowerController, cmd.boolVal=false
//   "Alexa, set the Hallway Dimmer to 30%"    -> PercentageController, cmd.intVal=30
//
// HARDWARE WIRING ASSUMPTION: the power relay is driven on Particle pin D7
// (set OUTPUT in setup()); the actual dimming hardware (phase-cut / PWM driver)
// is wired in applyPercentage().
//
// GOTCHAS:
//   - .ino prototype rule: onControl()'s signature spells out
//     `automatica::CtlCommand` because the Arduino preprocessor hoists a forward
//     prototype above the `using namespace automatica;` line.
//   - Both capabilities are singletons: in the callback match on cmd.code; the
//     instance arg is kNoInstance (do not read cmd.instance).
//   - Power and Percentage are independent caps — Alexa may set the level while
//     the device is off; you decide whether to apply it immediately or on next on.
// =============================================================================
#include "automatica.h"
#include "AutomaticaCloud.h"

using namespace automatica;

Automatica home;                 // the core facade. Never name it 'automatica' (clashes with the namespace).
AutomaticaCloud cloud(home);     // Particle adapter; its ctor calls home.setCloudPort(this).

// --- hardware stubs: replace with your relay + phase-cut/PWM dimmer driver ---
static void applyPower(bool on)       { digitalWrite(D7, on ? HIGH : LOW); }  // power relay on Particle pin D7
static void applyPercentage(int pct)  { /* map 0..100 -> your dimmer level */ (void)pct; }

// Control callback: invoked once per validated directive. Return true = handled
// (success); false = unhandled (Lambda returns an Alexa error). Dispatch is on
// cmd.code because both capabilities are singletons (cmd.instance == kNoInstance).
// Signature uses fully-qualified automatica::CtlCommand per the .ino prototype gotcha.
static bool onControl(const automatica::CtlCommand& cmd, void*) {
    switch (cmd.code) {
        case kCapPower:      applyPower(cmd.boolVal);      return true;  // 'o' — cmd.boolVal = on/off
        case kCapPercentage: applyPercentage(cmd.intVal);  return true;  // 'p' — cmd.intVal = level 0..100
    }
    return false;   // unknown/unhandled -> Lambda returns an Alexa error
}

void setup() {
    pinMode(D7, OUTPUT);   // power relay output (matches applyPower())

    // addEndpoint(id, friendlyName, description, displayCategories) -> endpoint idx.
    // Pass the returned idx to each addXxx() builder so the caps attach to THIS endpoint.
    int dimmer = home.addEndpoint("hallway_dimmer", "Hallway Dimmer", "automatica percentage dimmer", {"LIGHT"});

    home.addPower(dimmer);       // 'o' PowerController — singleton, on/off
    home.addPercentage(dimmer);  // 'p' PercentageController — singleton, 0..100 percent

    // Initial state (singletons -> instance arg is kNoInstance): off, level 100%.
    { CapState s; s.b = false; home.setInitialState(dimmer, kCapPower,      kNoInstance, s); }  // CapState.b = power off
    { CapState s; s.i = 100;   home.setInitialState(dimmer, kCapPercentage, kNoInstance, s); }  // CapState.i = level 100%

    home.onControl(onControl);  // register the dispatcher above
    home.begin();               // build the manifest + register Particle variables/function (call once)
}

void loop() {
    home.loop();   // drive the core: flush coalesced publishes + emit the initial snapshot
}
