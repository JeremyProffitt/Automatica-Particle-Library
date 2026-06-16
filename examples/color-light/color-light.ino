// =============================================================================
// color-light.ino — automatica example
// Device archetype: RGB lamp / color light (e.g. a NeoPixel strip), one endpoint,
// display category "LIGHT". One clearly-scoped concept: color + brightness control
// of a single light, all via SINGLETON capabilities.
//
// ALEXA CAPABILITIES EXPOSED (all SINGLETON — one per endpoint, no instance):
//   • Alexa.PowerController      (code 'o', kCapPower) — TurnOn/TurnOff (bool).
//   • Alexa.BrightnessController (code 'b', kCapBrightness) — level 0..100 (int).
//   • Alexa.ColorController      (code 'c', kCapColor) — sets HUE + SATURATION.
//       cmd.hue 0..360 (degrees), cmd.sat 0..100 (percent).
//
// BEHAVIORS DEMONSTRATED:
//   • Setting a COLOR also turns the light ON (if it was off).
//   • Power / brightness / color are persisted via the unified LightSettings store
//     (emulated EEPROM on Particle) and RESTORED on reset. Flip PERSIST_SETTINGS off
//     to disable.
//
// NOTE: there is no separate "color brightness" — SetColor carries hue+sat only, and
// the 'b' BrightnessController owns the brightness level. We hold the last hue/sat/bri
// so a change to any one repaints the strip with the others.
//
// ALEXA UTTERANCES THIS ENABLES:
//   "Alexa, turn on the lamp"
//   "Alexa, set the lamp to blue"     (ColorController -> hue/sat, also turns it on)
//   "Alexa, set the lamp to 50%"      (BrightnessController -> intVal=50)
//   "Alexa, dim the lamp"             (BrightnessController, relative adjust)
//
// HARDWARE WIRING ASSUMPTION:
//   A NeoPixel / addressable RGB strip driven by your own setStripHsb() (stub).
//   The neopixel library (#include below) is the typical driver; left commented
//   so the example compiles without the extra dependency. "Power off" = brightness 0.
//
// GOTCHAS:
//   • All three caps are SINGLETONS: addressed by cmd.code in the callback.
//   • LightSettings is the single owner of persisted state; add fields there (and bump
//     its version) instead of writing your own EEPROM blobs, so features never clobber
//     each other.
//   • .ino preprocessor prototype rule: keep onControl's parameter fully qualified
//     as automatica::CtlCommand so the auto-generated prototype matches the definition.
// =============================================================================
#include "automatica.h"
#include "AutomaticaCloud.h"
#include "LightSettings.h"   // unified persisted-settings store (EEPROM-backed on Particle)
// #include <neopixel.h>     // library depends on neopixel 1.0.4

using namespace automatica;

// ---- persistence master switch: flip to false to disable all save+restore ----
static const bool PERSIST_SETTINGS = true;

// Core facade + Particle cloud adapter. Never name the facade 'automatica'
// (namespace clash). The adapter ctor calls home.setCloudPort(this).
Automatica home;
AutomaticaCloud cloud(home);

static int  gEp = -1;
static bool gPower = false;
static int  gHue = 0, gSat = 0, gBri = 100;   // hue 0..360, sat 0..100, bri 0..100

// ---- unified persisted settings (deferred/coalesced saves) ----
static LightSettings settings("color_lamp");
static bool     gDirty = false;
static uint32_t gDirtyAt = 0;
static void markDirty() { gDirty = true; gDirtyAt = millis(); }
static void persistNow() {
    settings.state.power = gPower ? 1 : 0;
    settings.state.bri   = (int16_t)gBri;
    settings.state.hue   = (int16_t)gHue;
    settings.state.sat   = (int16_t)gSat;
    settings.save();
}
static void applyRestoredSettings() {
    gPower = settings.state.power != 0;
    gBri   = settings.state.bri;
    gHue   = settings.state.hue;
    gSat   = settings.state.sat;
}

// --- hardware stub: replace with your Adafruit NeoPixel driver ---
// hue 0..360, sat 0..100, bri 0..100 -> convert to RGB and push to the strip.
static void setStripHsb(int hue, int sat, int bri) { (void)hue; (void)sat; (void)bri; }
// Render current state; "off" = brightness 0 while preserving stored hue/sat.
static void render() { setStripHsb(gHue, gSat, gPower ? gBri : 0); }

// Push current power/brightness/color back to Alexa (used after the color->on auto-power).
static void pushStateToAlexa() {
    { CapState s; s.b   = gPower;             home.setInitialState(gEp, kCapPower,      kNoInstance, s); }
    { CapState s; s.i   = gBri;               home.setInitialState(gEp, kCapBrightness, kNoInstance, s); }
    { CapState s; s.hue = gHue; s.sat = gSat; home.setInitialState(gEp, kCapColor,      kNoInstance, s); }
    home.reportState(gEp);
}

// Control callback. Dispatch on cmd.code (all singletons).
// Fully-qualify CtlCommand (the .ino preprocessor emits the prototype before 'using namespace').
static bool onControl(const automatica::CtlCommand& cmd, void*) {
    switch (cmd.code) {
        case kCapPower:      gPower = cmd.boolVal;             render(); markDirty(); return true; // 'o'
        case kCapBrightness: gBri   = cmd.intVal;             render(); markDirty(); return true; // 'b'
        case kCapColor:      // setting a color also ensures the light is ON
            gHue = cmd.hue; gSat = cmd.sat; gPower = true;
            render(); pushStateToAlexa(); markDirty();
            return true;                                                                            // 'c'
    }
    return false;   // unknown/unhandled -> Lambda returns an Alexa error
}

void setup() {
    settings.begin(PERSIST_SETTINGS);
    if (settings.load()) applyRestoredSettings();   // restore on reset

    gEp = home.addEndpoint("color_lamp", "Lamp", "automatica color light", {"LIGHT"});
    home.addPower(gEp);        // 'o' PowerController: TurnOn/TurnOff
    home.addBrightness(gEp);   // 'b' BrightnessController: level 0..100
    home.addColor(gEp);        // 'c' ColorController: hue 0..360 + sat 0..100

    // Seed the manifest's initial reported state from the RESTORED values.
    { CapState s; s.b   = gPower;             home.setInitialState(gEp, kCapPower,      kNoInstance, s); }
    { CapState s; s.i   = gBri;               home.setInitialState(gEp, kCapBrightness, kNoInstance, s); }
    { CapState s; s.hue = gHue; s.sat = gSat; home.setInitialState(gEp, kCapColor,      kNoInstance, s); }

    home.onControl(onControl);  // register dispatch callback
    home.begin();               // build manifest + register cloud var/function; call once
    render();                   // show the restored state immediately
}

void loop() {
    home.loop();

    // Deferred EEPROM save: flush ~1.5 s after the last change (coalesces edits).
    if (gDirty && millis() - gDirtyAt > 1500) {
        gDirty = false;
        persistNow();
    }
}
