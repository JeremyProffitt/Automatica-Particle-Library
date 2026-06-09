// dimmable-light.ino — automatica example
// ============================================================================
// DEVICE ARCHETYPE: a single dimmable lamp (relay/SSR for on-off + PWM dimmer)
// modeled as one endpoint with two SINGLETON capabilities.
//
// ALEXA CAPABILITIES EXPOSED (singletons — at most one per endpoint, addressed
// by capability CODE, never by instance):
//   'o' Alexa.PowerController       — TurnOn / TurnOff   (control: cmd.boolVal)
//   'b' Alexa.BrightnessController  — 0..100 percent      (control: cmd.intVal)
//
// EXACT ALEXA UTTERANCES THIS ENABLES:
//   "Alexa, turn on the desk lamp"       -> PowerController TurnOn  (boolVal=true)
//   "Alexa, turn off the desk lamp"      -> PowerController TurnOff (boolVal=false)
//   "Alexa, set the desk lamp to 40%"    -> BrightnessController SetBrightness (intVal=40)
//   "Alexa, dim the desk lamp"           -> BrightnessController AdjustBrightness (intVal=resolved %)
//   "Alexa, brighten the desk lamp"      -> BrightnessController AdjustBrightness (intVal=resolved %)
//
// HARDWARE WIRING ASSUMPTION: on-off relay/SSR on pin D7 (driven HIGH = on).
// applyBrightness() is a stub — map 0..100 to your PWM duty cycle / dimmer.
//
// GOTCHAS:
//   - Singleton vs instanced: power and brightness are singletons, so the control
//     callback dispatches purely on cmd.code; cmd.instance is kNoInstance and is
//     not used. (Instanced caps like RangeController would also test cmd.instance.)
//   - .ino preprocessor rule: auto-generated prototypes are inserted ABOVE
//     'using namespace automatica;', so the callback's parameter type must be
//     FULLY QUALIFIED (automatica::CtlCommand) for the generated prototype to compile.
// ============================================================================
#include "automatica.h"
#include "AutomaticaCloud.h"

using namespace automatica;

Automatica home;                 // the core facade; never name it 'automatica' (clashes with the namespace)
AutomaticaCloud cloud(home);     // Particle Device-OS adapter; its ctor calls home.setCloudPort(this)

// --- hardware stubs: replace with your relay + PWM dimmer driver ---
// Signatures/behavior must stay as-is; only the bodies are yours to fill in.
static void applyPower(bool on)     { digitalWrite(D7, on ? HIGH : LOW); }       // on=true -> relay HIGH
static void applyBrightness(int pct) { /* map 0..100 -> PWM duty */ (void)pct; } // pct is 0..100

// Control callback: invoked once per validated inbound directive. Reads:
//   cmd.code    — capability code; dispatches the switch below
//   cmd.boolVal — PowerController on/off state (kCapPower)
//   cmd.intVal  — BrightnessController target 0..100 (kCapBrightness; Set/Adjust pre-resolved)
//   (cmd.instance is kNoInstance here — both caps are singletons, routed by code only)
// Return true => handled (success reported to Alexa); false => unhandled (Alexa error).
// Fully-qualify CtlCommand (the .ino preprocessor emits the prototype before 'using namespace').
static bool onControl(const automatica::CtlCommand& cmd, void*) {
    switch (cmd.code) {
        case kCapPower:      applyPower(cmd.boolVal);     return true;
        case kCapBrightness: applyBrightness(cmd.intVal); return true;
    }
    return false;   // unknown/unhandled -> Lambda returns an Alexa error
}

void setup() {
    pinMode(D7, OUTPUT);   // relay/SSR output for PowerController

    // Register the endpoint. Returns the endpoint index (== Alexa endpoint idx);
    // declaration order is the stable device identity and must not change across reboots.
    //   id   "desk_lamp"                   — internal id, ^[a-z0-9_-]{1,24}$
    //   name "Desk Lamp"                   — Alexa friendlyName (what the user says)
    //   desc "automatica dimmable light"   — Alexa description
    //   cat  {"LIGHT"}                     — Alexa displayCategory
    int lamp = home.addEndpoint("desk_lamp", "Desk Lamp", "automatica dimmable light", {"LIGHT"});

    // Singleton capability builders. Each appends to endpoint `lamp` and returns 0
    // (singletons have no instance index — they are addressed by code, so the return
    // value is intentionally ignored).
    home.addPower(lamp);        // 'o' Alexa.PowerController       (TurnOn/TurnOff)
    home.addBrightness(lamp);   // 'b' Alexa.BrightnessController  (0..100 %)

    // Initial state: off, full brightness (reflected in the first manifest + snapshot).
    // For singletons pass the capability CODE with instance == kNoInstance.
    //   CapState.b = bool value (power)        CapState.i = int value (brightness %)
    { CapState s; s.b = false; home.setInitialState(lamp, kCapPower,      kNoInstance, s); }
    { CapState s; s.i = 100;   home.setInitialState(lamp, kCapBrightness, kNoInstance, s); }

    home.onControl(onControl);   // register the inbound-directive callback
    home.begin();                // build the manifest + register Particle var/function (call once)
}

void loop() {
    home.loop();                 // flush debounced state publishes + emit the initial snapshot
}
