// =============================================================================
// color-light.ino — automatica example
// Device archetype: RGB lamp / color light (e.g. a NeoPixel strip), one endpoint,
// display category "LIGHT". One clearly-scoped concept: color + brightness control
// of a single light, all via SINGLETON capabilities.
//
// ALEXA CAPABILITIES EXPOSED (all SINGLETON — one per endpoint, no instance):
//   • Alexa.PowerController      (code 'o', kCapPower) — TurnOn/TurnOff (bool).
//   • Alexa.BrightnessController (code 'b', kCapBrightness) — level 0..100 (int).
//       cmd.intVal carries the brightness percent.
//   • Alexa.ColorController      (code 'c', kCapColor) — sets HUE + SATURATION.
//       cmd.hue 0..360 (degrees), cmd.sat 0..100 (percent). cmd.bri is also
//       present on the wire but here we KEEP brightness on the shared 'b' cap.
//   NOTE: there is no separate "color brightness" — SetColor carries hue+sat only,
//   and the 'b' BrightnessController owns the brightness level. We hold the last
//   hue/sat/bri so a change to any one repaints the strip with the other two.
//
// ALEXA UTTERANCES THIS ENABLES:
//   "Alexa, turn on the lamp"
//   "Alexa, set the lamp to blue"     (ColorController -> cmd.hue/cmd.sat)
//   "Alexa, set the lamp to 50%"      (BrightnessController -> cmd.intVal=50)
//   "Alexa, dim the lamp"             (BrightnessController, relative adjust)
//
// HARDWARE WIRING ASSUMPTION:
//   A NeoPixel / addressable RGB strip driven by your own setStripHsb() (stub).
//   The neopixel library (#include below) is the typical driver; left commented
//   so the example compiles without the extra dependency. No power pin is toggled
//   here — "power off" is rendered as brightness 0 on the strip.
//
// GOTCHAS:
//   • All three caps are SINGLETONS: addressed by cmd.code in the callback;
//     cmd.instance is kNoInstance and is not used.
//   • Brightness is SHARED — do not add a second brightness under color. Reading
//     cmd.bri from a SetColor directive would double-own the level; we ignore it.
//   • We mirror hue/sat/bri in globals because each directive only updates one of
//     them; the strip needs all three to repaint.
//   • .ino preprocessor prototype rule: keep onControl's parameter fully qualified
//     as automatica::CtlCommand so the auto-generated prototype (emitted above the
//     `using namespace automatica;` line) matches the definition.
// =============================================================================
#include "automatica.h"
#include "AutomaticaCloud.h"
// #include <neopixel.h>   // library depends on neopixel 1.0.4

using namespace automatica;

// Core facade + Particle cloud adapter. Never name the facade 'automatica'
// (namespace clash). The adapter ctor calls home.setCloudPort(this).
Automatica home;                 // never name it 'automatica' (namespace clash)
AutomaticaCloud cloud(home);

// Last-known hue/sat/brightness, so a color or brightness change can repaint the
// strip with the unchanged components. hue 0..360, sat 0..100, bri 0..100.
static int gHue = 0, gSat = 0, gBri = 100;

// --- hardware stubs: replace with your Adafruit NeoPixel driver ---
// hue 0..360, sat 0..100, bri 0..100 -> convert to RGB and push to the strip.
static void setStripHsb(int hue, int sat, int bri) { (void)hue; (void)sat; (void)bri; }
// "Off" = paint the strip at brightness 0 while preserving the stored hue/sat.
static void applyPower(bool on) { setStripHsb(gHue, gSat, on ? gBri : 0); }

// Control callback. Reads:
//   cmd.code    — capability code; dispatch.
//   cmd.boolVal — 'o' PowerController (true=on -> repaint at gBri, false -> 0).
//   cmd.intVal  — 'b' BrightnessController level 0..100.
//   cmd.hue     — 'c' ColorController hue 0..360 (degrees).
//   cmd.sat     — 'c' ColorController saturation 0..100 (percent).
//   cmd.instance— kNoInstance (all singletons); not read.
// Return true = handled (facade applies + reports state).
// Fully-qualify CtlCommand (the .ino preprocessor emits the prototype before 'using namespace').
static bool onControl(const automatica::CtlCommand& cmd, void*) {
    switch (cmd.code) {
        case kCapPower:      applyPower(cmd.boolVal); return true;                                   // 'o'
        case kCapBrightness: gBri = cmd.intVal; setStripHsb(gHue, gSat, gBri); return true;          // 'b' level 0..100
        case kCapColor:      gHue = cmd.hue; gSat = cmd.sat; setStripHsb(gHue, gSat, gBri); return true; // 'c' hue+sat
    }
    return false;   // unknown/unhandled -> Lambda returns an Alexa error
}

void setup() {
    // Register the endpoint (idx == Alexa idx; declaration order is the device identity).
    int lamp = home.addEndpoint("color_lamp", "Lamp", "automatica color light", {"LIGHT"});

    // Declare the three SINGLETON capabilities. Each returns 0 (unused) — all are
    // addressed by cmd.code, not by an instance index.
    home.addPower(lamp);       // 'o' PowerController: TurnOn/TurnOff
    home.addBrightness(lamp);  // 'b' BrightnessController: level 0..100
    home.addColor(lamp);       // 'c' ColorController: hue 0..360 + sat 0..100

    // Initial state: off, white-ish (sat 0), full brightness. Singletons -> kNoInstance.
    //   CapState.b   -> Power bool.
    //   CapState.i   -> Brightness level 0..100.
    //   CapState.hue/.sat -> Color hue/sat (sat 0 = white/unsaturated).
    { CapState s; s.b = false;              home.setInitialState(lamp, kCapPower,      kNoInstance, s); }
    { CapState s; s.i = 100;                home.setInitialState(lamp, kCapBrightness, kNoInstance, s); }
    { CapState s; s.hue = 0; s.sat = 0;     home.setInitialState(lamp, kCapColor,      kNoInstance, s); }

    home.onControl(onControl);  // register dispatch callback
    home.begin();               // build manifest + register cloud var/function; call once
}

void loop() {
    home.loop();
}
