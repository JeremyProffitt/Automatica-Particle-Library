// tunable-white-light.ino — automatica example.
//
// DEVICE ARCHETYPE: a tunable-white light (warm <-> cool white, no RGB color),
// modelled as ONE endpoint with on/off, dimming, and color-temperature control.
//
// ALEXA CAPABILITIES EXPOSED (three singletons on this one endpoint):
//   'o' addPower              -> Alexa.PowerController            (CONTROLLABLE; TurnOn/TurnOff).
//        Control: cmd.boolVal (true=on). State (CapState): .b (bool).
//   'b' addBrightness         -> Alexa.BrightnessController       (CONTROLLABLE; 0..100, percent).
//        Control: cmd.intVal (0..100). State (CapState): .i (int 0..100).
//   'k' addColorTemperature   -> Alexa.ColorTemperatureController (CONTROLLABLE; 1000..10000 K).
//        Control: cmd.intVal (kelvin; LOWER = warmer/amber, HIGHER = cooler/blue).
//        State (CapState): .i (int kelvin). Range/units: 1000..10000 Kelvin.
//   displayCategory: LIGHT.
//
// ALEXA UTTERANCES ENABLED:
//   "Alexa, turn on the light"              -> PowerController TurnOn  (kCapPower, boolVal=true)
//   "Alexa, set the light to 60%"           -> BrightnessController    (kCapBrightness, intVal=60)
//   "Alexa, dim the light"                  -> BrightnessController    (kCapBrightness, intVal=...)
//   "Alexa, set the light to 4000 kelvin"   -> ColorTemperatureController (kCapColorTemperature, intVal=4000)
//   "Alexa, make the light warmer"          -> ColorTemperatureController (DECREASE kelvin)
//   "Alexa, make the light cooler"          -> ColorTemperatureController (INCREASE kelvin)
//
// HARDWARE WIRING ASSUMPTION: a dual-channel warm/cool PWM driver. D7 here gates power
// (applyPower); applyBrightness sets overall duty; applyColorTemp blends the warm vs
// cool channels for the requested kelvin. D7 is set OUTPUT in setup().
//
// GOTCHAS:
//   * THREE SINGLETONS, ONE ENDPOINT: each capability is dispatched by cmd.code with
//     cmd.instance == kNoInstance — never by an instance index. The add* builders return 0.
//   * This is COLOR-TEMPERATURE (white tuning), NOT a ColorController ('c'/RGB). "warmer"/
//     "cooler" map to kelvin on 'k'; there is no hue/saturation here.
//   * KELVIN POLARITY: lower kelvin is WARMER, higher is COOLER — counter to "warm = high".
//   * .ino PROTOTYPE RULE: the Arduino preprocessor inserts onControl's auto-prototype
//     above `using namespace automatica;`, so its parameter is fully qualified as
//     automatica::CtlCommand here.
#include "automatica.h"
#include "AutomaticaCloud.h"

using namespace automatica;

Automatica home;                 // the core facade. Never name it 'automatica' (clashes with the namespace).
AutomaticaCloud cloud(home);     // ctor wires the Particle cloud surface (variable/function/publish).

// --- hardware stubs: replace with your dual-channel (warm/cool) PWM driver ---
static void applyPower(bool on)        { digitalWrite(D7, on ? HIGH : LOW); }       // relay/enable on D7: HIGH=on, LOW=off.
static void applyBrightness(int pct)   { /* map 0..100 -> overall PWM duty */ (void)pct; }
// kelvin 1000..10000 -> blend warm/cool channels (low K = warm, high K = cool).
static void applyColorTemp(int kelvin) { (void)kelvin; }

// Control callback. Signature: bool(const CtlCommand&, void* ctx); return true =
// "handled". Dispatch on cmd.code; all three capabilities are singletons so
// cmd.instance is kNoInstance and is not inspected.
// Fully-qualify CtlCommand (the .ino preprocessor emits the prototype before 'using namespace').
static bool onControl(const automatica::CtlCommand& cmd, void*) {
    switch (cmd.code) {
        case kCapPower:            applyPower(cmd.boolVal);     return true; // boolVal: true=on
        case kCapBrightness:       applyBrightness(cmd.intVal); return true; // intVal: 0..100 (percent)
        case kCapColorTemperature: applyColorTemp(cmd.intVal);  return true; // intVal: 1000..10000 (kelvin)
    }
    return false;   // unknown/unhandled -> Lambda returns an Alexa error
}

void setup() {
    pinMode(D7, OUTPUT);         // power/enable pin.

    // addEndpoint(id, friendlyName, description, {displayCategories}) -> endpoint idx.
    //   id "white_light" : stable device id (^[a-z0-9_-]{1,24}$); must persist across reboots.
    //   category LIGHT drives the Alexa app's tile/icon.
    int light = home.addEndpoint("white_light", "Light", "automatica tunable white light", {"LIGHT"});

    home.addPower(light);            // 'o' PowerController (singleton, returns 0/ignored).
    home.addBrightness(light);       // 'b' BrightnessController, 0..100 (singleton, returns 0/ignored).
    home.addColorTemperature(light); // 'k' ColorTemperatureController, 1000..10000 K (singleton, returns 0/ignored).

    // Initial state: off, full brightness, neutral-white 4000 K. Each singleton is
    // seeded by code + kNoInstance so the first manifest/snapshot is accurate.
    { CapState s; s.b = false; home.setInitialState(light, kCapPower,            kNoInstance, s); } // .b   = power bool
    { CapState s; s.i = 100;   home.setInitialState(light, kCapBrightness,       kNoInstance, s); } // .i   = 0..100 percent
    { CapState s; s.i = 4000;  home.setInitialState(light, kCapColorTemperature, kNoInstance, s); } // .i   = kelvin

    home.onControl(onControl);   // register the control callback.
    home.begin();                // build manifest + register cloud var/function. Call once, after all add*().
}

void loop() {
    home.loop();                // REQUIRED every loop: flush debounced publishes + emit the initial snapshot.
}
