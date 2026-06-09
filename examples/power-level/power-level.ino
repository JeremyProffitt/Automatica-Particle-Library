// power-level.ino — automatica example
// =====================================
// Device archetype: a single VARIABLE-POWER LOAD (e.g. a workshop heater element,
// a variable-speed motor, a dimmable resistive element) on one Alexa endpoint.
//
// Concept (one per example): the PowerLevelController interface — a plain 0..100
// integer "power level". This is a DISTINCT Alexa interface from
// BrightnessController ('b', for lamps) and PercentageController ('p', for fans/
// generic %). Use PowerLevelController for loads Alexa naturally addresses as a
// "power level" (heaters, motors, variable elements).
//
// Alexa capabilities exposed on this endpoint:
//   - Alexa.PowerController       (code 'o', kCapPower)      — singleton, bool on/off.
//                                                              No instance string.
//   - Alexa.PowerLevelController  (code 'w', kCapPowerLevel) — singleton, int 0..100
//                                                              (unitless level), supports
//                                                              SetPowerLevel (absolute) and
//                                                              AdjustPowerLevel (relative delta).
//                                                              No instance string.
//
// Both are SINGLETON capabilities: at most one per endpoint, addressed by their
// capability CODE (cmd.code) in the control callback, NOT by an instance index.
// Their wire instance is kNoInstance (-1); the int the add* builder returns (0) is
// unused for singletons.
//
// Exact Alexa utterances this enables:
//   "Alexa, turn on the workshop heater"               -> PowerController TurnOn  (boolVal=true)
//   "Alexa, turn off the workshop heater"              -> PowerController TurnOff (boolVal=false)
//   "Alexa, set the workshop heater power to 70%"      -> PowerLevelController SetPowerLevel    (intVal=70)
//   "Alexa, increase the workshop heater power by 10"  -> PowerLevelController AdjustPowerLevel (intVal=70, pre-resolved absolute)
//   "Alexa, decrease the workshop heater power by 10"  -> PowerLevelController AdjustPowerLevel (intVal pre-resolved absolute)
// (The Lambda resolves Adjust* against last-known state and delivers an absolute
//  0..100 value in cmd.intVal, so the sketch only ever applies an absolute level.)
//
// Hardware wiring assumption: D7 drives the on/off relay (the on-board LED on most
// Particle boards). The variable-power stage (PWM pin or SSR phase-control driver)
// is left as a stub — wire applyPowerLevel() to your duty-cycle output.
#include "automatica.h"
#include "AutomaticaCloud.h"

using namespace automatica;

Automatica home;                 // the facade. Never name it 'automatica' — that
                                 // collides with the namespace.
AutomaticaCloud cloud(home);     // Particle adapter; its ctor calls home.setCloudPort(this).

// --- hardware stubs: replace with your relay + variable-power (PWM/SSR) driver ---
static void applyPower(bool on)        { digitalWrite(D7, on ? HIGH : LOW); }  // on/off relay
static void applyPowerLevel(int level) { /* map 0..100 -> element duty cycle */ (void)level; }

// Control callback. Invoked by the library after it decodes+validates an incoming
// automaticaCtl command; only fields valid for cmd.code are populated (SPEC §1.6).
//   - cmd.code     : which capability the directive targets ('o' or 'w' here).
//   - cmd.instance : wire instance index; kNoInstance (-1) for these singletons.
//   - cmd.boolVal  : the on/off value for PowerController ('o').
//   - cmd.intVal   : the absolute 0..100 level for PowerLevelController ('w').
// Return true = "handled" (library applies/reports state). Return false = unhandled
// -> the Lambda returns an Alexa error to the user.
//
// Gotcha: CtlCommand is fully-qualified as automatica::CtlCommand on purpose. The
// .ino preprocessor auto-generates this function's prototype and places it ABOVE
// the `using namespace automatica;` line, so an unqualified `CtlCommand` there would
// not resolve. (Singletons dispatch on cmd.code; no instance compare is needed.)
static bool onControl(const automatica::CtlCommand& cmd, void*) {
    switch (cmd.code) {
        case kCapPower:      applyPower(cmd.boolVal);       return true;
        case kCapPowerLevel: applyPowerLevel(cmd.intVal);   return true;
    }
    return false;   // unknown/unhandled -> Lambda returns an Alexa error
}

void setup() {
    pinMode(D7, OUTPUT);

    // Register the endpoint. Args: stable id (^[a-z0-9_-]{1,24}$, part of the device
    // identity — keep it constant across reboots), Alexa friendlyName, description,
    // and displayCategories ({"OTHER"} — Alexa has no dedicated heater category).
    // Returns the endpoint idx (== Alexa endpoint index); pass it to every builder.
    int heater = home.addEndpoint("workshop_heater", "Workshop Heater", "automatica power-level load", {"OTHER"});

    home.addPower(heater);       // 'o' Alexa.PowerController — singleton bool on/off.
    home.addPowerLevel(heater);  // 'w' Alexa.PowerLevelController — singleton int 0..100.
                                 // Both return 0 (unused: singletons route by cmd.code).

    // Seed the initial reported state BEFORE begin(). For singletons pass the cap
    // code with instance == kNoInstance. The populated CapState field must match the
    // capability's value type (SPEC §1.5):
    //   - PowerController ('o') uses CapState.b (bool).
    //   - PowerLevelController ('w') uses CapState.i (int 0..100).
    { CapState s; s.b = false; home.setInitialState(heater, kCapPower,      kNoInstance, s); }  // off
    { CapState s; s.i = 0;     home.setInitialState(heater, kCapPowerLevel, kNoInstance, s); }  // 0%

    home.onControl(onControl);   // register the control callback (ctx defaults to 0).
    home.begin();                // build the manifest + register cloud variables/function. Call once.
}

void loop() {
    home.loop();   // drives debounced state publishes (≤1/sec) and the initial snapshot.
}
