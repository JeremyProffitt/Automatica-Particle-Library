// addressable-led.ino — automatica example: a WS2812/NeoPixel addressable RGB strip
// controlled via the automatica facade (power + brightness + color).
//
// DEVICE ARCHETYPE: addressable RGB LED strip (display category "LIGHT"), one endpoint.
//
// CAPABILITIES SHOWN:
//   'o' Alexa.PowerController      — TurnOn / TurnOff. "Off" drives brightness 0 while
//                                    preserving the stored hue/sat so the next TurnOn
//                                    restores the last color.
//   'b' Alexa.BrightnessController — 0..100 mapped to 0..255 NeoPixel brightness.
//                                    Also wakes the strip from off (boolVal → true).
//   'c' Alexa.ColorController      — sets hue 0..360 + sat 0..100; also turns the strip
//                                    on (Alexa convention: "set to blue" should be visible).
//                                    pushStateToAlexa() syncs the Power reported-state back.
//
// ALEXA UTTERANCES ENABLED:
//   "Alexa, turn on the LED strip"
//   "Alexa, turn off the LED strip"
//   "Alexa, set the LED strip to 50%"       (BrightnessController → cmd.intVal, also on)
//   "Alexa, dim the LED strip"              (BrightnessController, relative adjust)
//   "Alexa, brighten the LED strip"         (BrightnessController, relative adjust)
//   "Alexa, set the LED strip to blue"      (ColorController → hue/sat, also turns on)
//   "Alexa, set the LED strip to red"       (ColorController)
//
// HARDWARE WIRING:
//   WS2812B strip, PIXEL_COUNT pixels, DATA on PIXEL_PIN (default A3 — matches the
//   illuminatica dev board). 5 V power; budget the strip's mA separately from the Photon.
//   Use the Particle neopixel community library (Adafruit fork, tested on Photon/Argon).
//
// PARTICLE DEVICE-OS NOTES (Photon/Argon/Boron):
//   SYSTEM_THREAD(ENABLED) lets setup()/loop() run immediately at boot, before the
//   cloud handshake — the strip self-test and color rendering work even while offline.
//   System.freeMemory() and System.uptime() are exposed as Particle.variable()s so
//   the Particle Console / CLI can poll device health without a custom serial line.
//   The last reset reason (System.resetReason()) is logged once at startup.
//
// GOTCHAS:
//   • All three capabilities are SINGLETONS: addressed by cmd.code in the callback
//     and in setInitialState() by the capability code + kNoInstance sentinel.
//   • Setting a COLOR (ColorController) also turns the strip ON per Alexa convention —
//     call pushStateToAlexa() so Alexa sees the auto-on in the reported state.
//   • .ino preprocessor prototype rule: onControl's parameter must be fully qualified
//     as automatica::CtlCommand; the Arduino preprocessor emits the prototype ABOVE
//     `using namespace automatica`, so an unqualified name would fail to compile.
//   • Adafruit_NeoPixel::setBrightness() sets a global brightness scaler. We drive it
//     directly (0..255) rather than scaling each pixel, which is simpler and avoids the
//     accumulated rounding loss of repeated scaling.
//   • Never name the Automatica object 'automatica' — it would clash with the namespace.
//     We use `home`.
//
// PARITY NOTE WITH ESP32 COUNTERPART:
//   The ESP32 addressable-led example adds StripEngine + LclDecoder + MQTT pattern
//   subscriptions (LCL v4 bytecode over AWS IoT). Those subsystems have no Particle
//   counterpart — the Particle transport is Particle.variable/function/publish via
//   AutomaticaCloud, not AWS IoT MQTT, so pattern-topic chaining does not apply.
//   This example therefore implements the same three Alexa capabilities (Power +
//   Brightness + Color) using the native Particle neopixel library; it is the correct
//   and complete Particle implementation of an addressable RGB strip endpoint.

// IMPORTANT: no library-namespace types in function signatures above the first
// #include — the .ino preprocessor inserts prototypes here, before `using namespace`.
#include "automatica.h"
#include "AutomaticaCloud.h"
#include "neopixel.h"   // Particle community neopixel library (Adafruit fork)

SYSTEM_THREAD(ENABLED);   // setup()/loop() run immediately; cloud connects in background

using namespace automatica;

// ---- Strip hardware constants -----------------------------------------------
// Change PIXEL_PIN and PIXEL_COUNT to match your wiring.
// PIXEL_TYPE: WS2812B (GRB) is most common; WS2812 (also GRB) is the same wire.
static const int  PIXEL_PIN   = A3;         // DATA line to the strip
static const int  PIXEL_COUNT = 24;         // number of addressable pixels
static const int  PIXEL_TYPE  = WS2812B;    // GRB byte order

Adafruit_NeoPixel strip(PIXEL_COUNT, PIXEL_PIN, PIXEL_TYPE);

// ---- automatica facade + Particle cloud adapter -----------------------------
// AutomaticaCloud ctor calls home.setCloudPort(this) — no separate wiring needed.
Automatica home;
AutomaticaCloud cloud(home);

// ---- Endpoint index + soft state -------------------------------------------
static int  gEp    = -1;
static bool gPower = false;
static int  gHue   = 0;    // degrees 0..360
static int  gSat   = 0;    // percent  0..100  (0 = white, 100 = fully saturated)
static int  gBri   = 100;  // percent  0..100  (mapped to 0..255 in render)

// ---- Device-OS vitals exposed to Particle Console --------------------------
// Declared as int so Particle.variable() can hold them (max 120-char string or int/double).
static int gFreeMemory = 0;
static int gUptimeSec  = 0;

// ---- HSV → RGB helper -------------------------------------------------------
// hue 0..360, sat 0..100, val 0..100 → r,g,b 0..255.
// Six-sector formula; produces the same result as the illuminatica reference.
static void hsvToRgb(int h, int s, int v, uint8_t& r, uint8_t& g, uint8_t& b) {
    float hf = (float)(h % 360) / 60.0f;
    float sf = (float)s / 100.0f;
    float vf = (float)v / 100.0f;
    int   i  = (int)hf;
    float f  = hf - i;
    float p  = vf * (1.0f - sf);
    float q  = vf * (1.0f - sf * f);
    float t  = vf * (1.0f - sf * (1.0f - f));
    float rf, gf, bf;
    switch (i) {
        case 0:  rf = vf; gf = t;  bf = p;  break;
        case 1:  rf = q;  gf = vf; bf = p;  break;
        case 2:  rf = p;  gf = vf; bf = t;  break;
        case 3:  rf = p;  gf = q;  bf = vf; break;
        case 4:  rf = t;  gf = p;  bf = vf; break;
        default: rf = vf; gf = p;  bf = q;  break;
    }
    r = (uint8_t)(rf * 255.0f + 0.5f);
    g = (uint8_t)(gf * 255.0f + 0.5f);
    b = (uint8_t)(bf * 255.0f + 0.5f);
}

// ---- Strip render -----------------------------------------------------------
// Converts the current soft state (gPower/gHue/gSat/gBri) to NeoPixel output.
// "Off" = setBrightness(0) so stored hue/sat survive a power cycle.
static void applyStrip() {
    if (!gPower) {
        strip.setBrightness(0);
    } else {
        strip.setBrightness((uint8_t)(gBri * 255 / 100));
        uint8_t r, g, b;
        hsvToRgb(gHue, gSat, 100, r, g, b);   // full-value HSV; brightness scaler handles level
        for (int i = 0; i < PIXEL_COUNT; i++) strip.setPixelColor(i, r, g, b);
    }
    strip.show();
}

// ---- Push current state back to Alexa ---------------------------------------
// Required after ColorController auto-turns the strip on, so Alexa's Power
// reported-state stays in sync with the physical device.
static void pushStateToAlexa() {
    { CapState s; s.b   = gPower;             home.setInitialState(gEp, kCapPower,      kNoInstance, s); }
    { CapState s; s.i   = gBri;               home.setInitialState(gEp, kCapBrightness, kNoInstance, s); }
    { CapState s; s.hue = gHue; s.sat = gSat; home.setInitialState(gEp, kCapColor,      kNoInstance, s); }
    home.reportState(gEp);
}

// ---- Boot rainbow self-test -------------------------------------------------
// Three rapid sweeps across the strip before the cloud connects — confirms LEDs,
// wiring, and power rail are all good at boot time.
static uint32_t wheelColor(uint8_t pos) {
    pos = 255 - pos;
    if (pos < 85)  return strip.Color(255 - pos * 3, 0, pos * 3);
    if (pos < 170) { pos -= 85;  return strip.Color(0, pos * 3, 255 - pos * 3); }
    pos -= 170;    return strip.Color(pos * 3, 255 - pos * 3, 0);
}
static void bootRainbow() {
    strip.setBrightness(80);   // moderate brightness for the self-test
    for (int cycle = 0; cycle < 3; cycle++) {
        for (int j = 0; j < 256; j += 4) {
            for (int i = 0; i < PIXEL_COUNT; i++)
                strip.setPixelColor(i, wheelColor((uint8_t)((i * 256 / PIXEL_COUNT + j) & 255)));
            strip.show();
            delay(12);
        }
    }
}

// ---- Control callback -------------------------------------------------------
// Fully-qualify CtlCommand: the .ino preprocessor emits the prototype ABOVE
// `using namespace automatica`, so the unqualified name would be unresolved.
static bool onControl(const automatica::CtlCommand& cmd, void*) {
    switch (cmd.code) {
        case kCapPower:      // 'o' TurnOn / TurnOff
            gPower = cmd.boolVal;
            applyStrip();
            return true;

        case kCapBrightness: // 'b' SetBrightness / AdjustBrightness 0..100
            gBri   = cmd.intVal;
            gPower = true;   // "set to 50%" implies on
            applyStrip();
            return true;

        case kCapColor:      // 'c' SetColor hue 0..360, sat 0..100
            gHue   = cmd.hue;
            gSat   = cmd.sat;
            gPower = true;   // "set to blue" turns the strip on per Alexa convention
            applyStrip();
            pushStateToAlexa();   // sync Power back so Alexa sees the auto-on
            return true;
    }
    return false;   // unknown capability → Lambda returns an Alexa error
}

// ---- setup ------------------------------------------------------------------
void setup() {
    Serial.begin(115200);

    // Log the last reset reason so crash/watchdog events are visible in the serial log.
    int reason = System.resetReason();
    Serial.printlnf("automatica addressable-led boot — resetReason=%d freeMemory=%u",
                    reason, (unsigned)System.freeMemory());

    // Expose device-health vitals to the Particle Console (polled from the cloud side).
    Particle.variable("freeMemory", gFreeMemory);   // int bytes remaining on heap
    Particle.variable("uptimeSec",  gUptimeSec);    // int seconds since last reset

    // Strip + boot rainbow self-test (runs before cloud; SYSTEM_THREAD lets this
    // proceed even while the Particle cloud handshake is in flight).
    strip.begin();
    strip.show();   // all pixels off while we initialise
    bootRainbow();

    // Register the Alexa endpoint. The stable id ("led_strip") and declaration order
    // ARE the device identity — they must not change across firmware updates.
    gEp = home.addEndpoint("led_strip", "LED Strip",
                            "automatica addressable LED strip", {"LIGHT"});
    home.addPower(gEp);        // 'o' Alexa.PowerController      — TurnOn/TurnOff
    home.addBrightness(gEp);   // 'b' Alexa.BrightnessController — 0..100
    home.addColor(gEp);        // 'c' Alexa.ColorController      — hue 0..360, sat 0..100

    // Seed the initial reported state (call before begin()).
    // Singletons → pass capability CODE + kNoInstance; no instance index needed.
    { CapState s; s.b   = gPower;             home.setInitialState(gEp, kCapPower,      kNoInstance, s); }
    { CapState s; s.i   = gBri;               home.setInitialState(gEp, kCapBrightness, kNoInstance, s); }
    { CapState s; s.hue = gHue; s.sat = gSat; home.setInitialState(gEp, kCapColor,      kNoInstance, s); }

    home.onControl(onControl);
    home.begin();     // build manifest + register Particle var/function; call once

    applyStrip();     // show initial state (off) after the self-test
}

// ---- loop -------------------------------------------------------------------
static unsigned long gLastVitals = 0;

void loop() {
    home.loop();   // REQUIRED: flush debounced state publishes + emit initial snapshot

    // Refresh vitals every 5 s so the Particle Console variables stay current.
    // System.freeMemory() and System.uptime() are Device-OS intrinsics — no sensor
    // driver needed. Cast to int because Particle.variable() stores them as int.
    if (millis() - gLastVitals >= 5000UL) {
        gLastVitals  = millis();
        gFreeMemory  = (int)System.freeMemory();
        gUptimeSec   = (int)(System.uptime());
    }
}
