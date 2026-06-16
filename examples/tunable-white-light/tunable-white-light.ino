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
// BEHAVIORS DEMONSTRATED:
//   * Setting the COLOR (temperature) also turns the light ON (if it was off).
//   * Power / brightness / color temperature are persisted via the unified LightSettings
//     store (emulated EEPROM on Particle) and RESTORED on reset. Flip PERSIST_SETTINGS off.
//
// ALEXA UTTERANCES ENABLED:
//   "Alexa, turn on the light"              -> PowerController TurnOn  (kCapPower, boolVal=true)
//   "Alexa, set the light to 60%"           -> BrightnessController    (kCapBrightness, intVal=60)
//   "Alexa, dim the light"                  -> BrightnessController    (kCapBrightness, intVal=...)
//   "Alexa, set the light to 4000 kelvin"   -> ColorTemperatureController (also turns it on)
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
//   * This is COLOR-TEMPERATURE (white tuning), NOT a ColorController ('c'/RGB).
//   * KELVIN POLARITY: lower kelvin is WARMER, higher is COOLER.
//   * LightSettings is the single owner of persisted state; extend it (and bump its
//     version) rather than writing your own EEPROM blobs.
//   * .ino PROTOTYPE RULE: onControl's parameter is fully qualified as automatica::CtlCommand.
#include "automatica.h"
#include "AutomaticaCloud.h"
#include "LightSettings.h"   // unified persisted-settings store (EEPROM-backed on Particle)

using namespace automatica;

// ---- persistence master switch: flip to false to disable all save+restore ----
static const bool PERSIST_SETTINGS = true;

Automatica home;                 // the core facade. Never name it 'automatica' (clashes with the namespace).
AutomaticaCloud cloud(home);     // ctor wires the Particle cloud surface (variable/function/publish).

static int  gEp     = -1;
static bool gPower  = false;
static int  gBri    = 100;     // 0..100 percent
static int  gKelvin = 4000;    // 1000..10000 K

// ---- unified persisted settings (deferred/coalesced saves) ----
static LightSettings settings("white_light");
static bool     gDirty = false;
static uint32_t gDirtyAt = 0;
static void markDirty() { gDirty = true; gDirtyAt = millis(); }
static void persistNow() {
    settings.state.power     = gPower ? 1 : 0;
    settings.state.bri       = (int16_t)gBri;
    settings.state.colorTemp = (int16_t)gKelvin;
    settings.save();
}
static void applyRestoredSettings() {
    gPower  = settings.state.power != 0;
    gBri    = settings.state.bri;
    gKelvin = settings.state.colorTemp;
}

// --- hardware stubs: replace with your dual-channel (warm/cool) PWM driver ---
static void applyPower(bool on)        { digitalWrite(D7, on ? HIGH : LOW); }       // relay/enable on D7
static void applyBrightness(int pct)   { /* map 0..100 -> overall PWM duty */ (void)pct; }
static void applyColorTemp(int kelvin) { /* 1000..10000 -> blend warm/cool */ (void)kelvin; }
// Drive the hardware to the current state ("off" => duty 0).
static void render() { applyPower(gPower); applyBrightness(gPower ? gBri : 0); applyColorTemp(gKelvin); }

// Push current power/brightness/color-temp back to Alexa (used after color-temp->on auto-power).
static void pushStateToAlexa() {
    { CapState s; s.b = gPower;   home.setInitialState(gEp, kCapPower,            kNoInstance, s); }
    { CapState s; s.i = gBri;     home.setInitialState(gEp, kCapBrightness,       kNoInstance, s); }
    { CapState s; s.i = gKelvin;  home.setInitialState(gEp, kCapColorTemperature, kNoInstance, s); }
    home.reportState(gEp);
}

// Control callback. Dispatch on cmd.code; all three capabilities are singletons.
// Fully-qualify CtlCommand (the .ino preprocessor emits the prototype before 'using namespace').
static bool onControl(const automatica::CtlCommand& cmd, void*) {
    switch (cmd.code) {
        case kCapPower:            gPower = cmd.boolVal; render(); markDirty(); return true; // boolVal: true=on
        case kCapBrightness:       gBri   = cmd.intVal;  render(); markDirty(); return true; // intVal: 0..100
        case kCapColorTemperature: // setting a color (temperature) also ensures the light is ON
            gKelvin = cmd.intVal; gPower = true;
            render(); pushStateToAlexa(); markDirty();
            return true;                                                                      // intVal: 1000..10000 K
    }
    return false;   // unknown/unhandled -> Lambda returns an Alexa error
}

void setup() {
    pinMode(D7, OUTPUT);         // power/enable pin.

    settings.begin(PERSIST_SETTINGS);
    if (settings.load()) applyRestoredSettings();   // restore on reset

    gEp = home.addEndpoint("white_light", "Light", "automatica tunable white light", {"LIGHT"});
    home.addPower(gEp);            // 'o' PowerController (singleton).
    home.addBrightness(gEp);       // 'b' BrightnessController, 0..100 (singleton).
    home.addColorTemperature(gEp); // 'k' ColorTemperatureController, 1000..10000 K (singleton).

    // Seed the manifest's initial reported state from the RESTORED values.
    { CapState s; s.b = gPower;  home.setInitialState(gEp, kCapPower,            kNoInstance, s); }
    { CapState s; s.i = gBri;    home.setInitialState(gEp, kCapBrightness,       kNoInstance, s); }
    { CapState s; s.i = gKelvin; home.setInitialState(gEp, kCapColorTemperature, kNoInstance, s); }

    home.onControl(onControl);   // register the control callback.
    home.begin();                // build manifest + register cloud var/function. Call once.
    render();                    // show the restored state immediately
}

void loop() {
    home.loop();                // REQUIRED every loop: flush debounced publishes + emit snapshot.

    // Deferred EEPROM save: flush ~1.5 s after the last change (coalesces edits).
    if (gDirty && millis() - gDirtyAt > 1500) {
        gDirty = false;
        persistNow();
    }
}
