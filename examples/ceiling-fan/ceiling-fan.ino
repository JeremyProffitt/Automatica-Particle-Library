// =============================================================================
// ceiling-fan.ino — automatica v2 example
// Device archetype: ceiling fan (one endpoint, display category "FAN").
// This is the canonical INSTANCED-capabilities example: it shows the three
// instanced capability types (Range, Toggle, Mode), each addressed by a named
// instance, alongside one singleton (Power).
//
// ALEXA CAPABILITIES EXPOSED:
//   • Alexa.PowerController  (code 'o', kCapPower) — SINGLETON. TurnOn/TurnOff (bool).
//   • Alexa.RangeController  (code 'r', kCapRange) — INSTANCED. instance "Fan.Speed".
//       Range 1..10, precision 1, unit "Percent" (Alexa asset catalog name).
//       Friendly names ("Speed", "Fan Speed") let the user say either.
//       cmd.intVal carries the requested speed (already validated to 1..10).
//   • Alexa.ToggleController (code 't', kCapToggle) — INSTANCED. instance "Fan.Oscillate".
//       On/off. Friendly names "Oscillate"/"Swivel". cmd.boolVal carries on/off.
//   • Alexa.ModeController   (code 'm', kCapMode) — INSTANCED. instance "Fan.Direction".
//       Unordered modes "FORWARD"/"REVERSE" (friendly: Forward/Summer, Reverse/Winter).
//       cmd.mode carries the selected mode VALUE string ("FORWARD" or "REVERSE").
//
// ALEXA UTTERANCES THIS ENABLES:
//   "Alexa, turn on the ceiling fan"
//   "Alexa, set the fan speed to 7"          (RangeController "Fan.Speed", 1..10)
//   "Alexa, turn on the fan oscillate"       (ToggleController "Fan.Oscillate")
//   "Alexa, set the fan direction to reverse" (ModeController "Fan.Direction")
//
// HARDWARE WIRING ASSUMPTION:
//   D7 drives fan power (HIGH = on; on-board LED on most Particle devices).
//   Speed/oscillate/direction are stubs — wire them to your motor driver.
//
// GOTCHAS:
//   • SINGLETON vs INSTANCED: addPower is a singleton (addressed by cmd.code only,
//     cmd.instance == kNoInstance). addRange/addToggle/addMode are INSTANCED — each
//     returns a wire instance index that you MUST keep and compare against
//     cmd.instance in the callback to know WHICH instance was addressed. Two ranges
//     on one endpoint would share code 'r' and differ only by instance index.
//   • The builder assigns instance indices in DECLARATION ORDER; capture each
//     returned value (gSpeedInst/gOscInst/gDirInst) rather than hard-coding 0,1,2.
//   • .ino preprocessor prototype rule: keep onControl's parameter fully qualified
//     as automatica::CtlCommand so the auto-generated prototype (emitted above the
//     `using namespace automatica;` line) matches the definition.
// =============================================================================
#include "automatica.h"
#include "AutomaticaCloud.h"

using namespace automatica;

// Core facade + Particle cloud adapter. Do not name the facade 'automatica'
// (collides with the namespace). The adapter ctor calls home.setCloudPort(this).
Automatica home;
AutomaticaCloud cloud(home);

// Wire instance indices returned by the INSTANCED builders. Saved at setup time
// and compared against cmd.instance in the callback to route the right control to
// the right capability instance. (Singletons need no such variable.)
int gSpeedInst, gOscInst, gDirInst;

// --- hardware stubs: replace with your motor/driver calls ---
static void applyPower(bool on)        { digitalWrite(D7, on ? HIGH : LOW); } // HIGH = fan on
static void applySpeed(int speed)      { /* PWM 1..10 -> duty */ (void)speed; } // speed validated to 1..10
static void applyOscillate(bool on)    { (void)on; }                            // true = oscillating
static void applyDirection(const std::string& dir) { (void)dir; }               // "FORWARD" | "REVERSE"

// Control callback. Reads:
//   cmd.code     — capability code; first-level dispatch.
//   cmd.instance — wire instance index for INSTANCED caps (r/t/m); compare to the
//                  saved gSpeedInst/gOscInst/gDirInst to identify the instance.
//                  (For the singleton 'o' this is kNoInstance and is not checked.)
//   cmd.boolVal  — 'o' Power and 't' Toggle payload (on/off).
//   cmd.intVal   — 'r' Range payload (the requested speed, validated to 1..10).
//   cmd.mode     — 'm' Mode payload (the selected mode VALUE string).
// Return true = handled (facade applies + reports state). Falling through to the
// final `return false` (e.g. an instance index we don't recognize) yields an Alexa error.
static bool onControl(const automatica::CtlCommand& cmd, void*) {
    switch (cmd.code) {
        case kCapPower:  applyPower(cmd.boolVal); return true;                                       // 'o' singleton
        case kCapRange:  if (cmd.instance == gSpeedInst) { applySpeed(cmd.intVal); return true; } break;     // 'r' Fan.Speed
        case kCapToggle: if (cmd.instance == gOscInst)   { applyOscillate(cmd.boolVal); return true; } break; // 't' Fan.Oscillate
        case kCapMode:   if (cmd.instance == gDirInst)   { applyDirection(cmd.mode); return true; } break;    // 'm' Fan.Direction
    }
    return false;   // unknown code or unrecognized instance -> Lambda returns an Alexa error
}

void setup() {
    pinMode(D7, OUTPUT);

    // Register the endpoint (idx == Alexa idx; declaration order is the device identity).
    int fan = home.addEndpoint("fan", "Ceiling Fan", "automatica ceiling fan", {"FAN"});

    // SINGLETON capability: PowerController. Returns 0; addressed by code, not instance.
    home.addPower(fan);

    // INSTANCED capability: RangeController "Fan.Speed".
    //   min/max     — inclusive value bounds (1..10) Alexa enforces before calling us.
    //   precision   — step size for "raise/lower" adjustments (1 = whole steps).
    //   unit        — Alexa asset-catalog unit name ("Percent"); "" for unitless.
    //   resources   — friendly names the user can speak ("Speed", "Fan Speed").
    // addRange returns the wire instance index; save it to route control later.
    RangeConfig speed;
    speed.min = 1; speed.max = 10; speed.precision = 1; speed.unit = "Percent";
    speed.resources.push_back("Speed");
    speed.resources.push_back("Fan Speed");
    gSpeedInst = home.addRange(fan, "Fan.Speed", speed);

    // INSTANCED capability: ToggleController "Fan.Oscillate".
    //   resources — friendly names ("Oscillate", "Swivel") for "turn on the fan <name>".
    // addToggle returns the wire instance index; save it for routing.
    ToggleConfig osc;
    osc.resources.push_back("Oscillate");
    osc.resources.push_back("Swivel");
    gOscInst = home.addToggle(fan, "Fan.Oscillate", osc);

    // INSTANCED capability: ModeController "Fan.Direction".
    //   ordered   — false: modes are a set, not a sequence (no implicit raise/lower).
    //   modes[]   — each ModeOption.value is the canonical VALUE (sent in cmd.mode),
    //               with resources[] the spoken friendly names for that value.
    //   resources — friendly names for the mode control itself ("Direction").
    // addMode returns the wire instance index; save it for routing.
    ModeConfig dir;
    dir.ordered = false;
    { ModeOption m; m.value = "FORWARD"; m.resources.push_back("Forward"); m.resources.push_back("Summer"); dir.modes.push_back(m); }
    { ModeOption m; m.value = "REVERSE"; m.resources.push_back("Reverse"); m.resources.push_back("Winter"); dir.modes.push_back(m); }
    dir.resources.push_back("Direction");
    gDirInst = home.addMode(fan, "Fan.Direction", dir);

    // Initial state (reflected in the first manifest + snapshot). Singletons use
    // kNoInstance; instanced caps use the saved wire instance index. The populated
    // CapState field depends on the cap: .b for Power/Toggle (bool), .i for Range
    // (int), .mode for Mode (the VALUE string).
    { CapState s; s.b = true;          home.setInitialState(fan, kCapPower,  kNoInstance, s); } // start on
    { CapState s; s.i = 7;             home.setInitialState(fan, kCapRange,  gSpeedInst,  s); } // speed 7 (1..10)
    { CapState s; s.b = false;         home.setInitialState(fan, kCapToggle, gOscInst,    s); } // not oscillating
    { CapState s; s.mode = "FORWARD";  home.setInitialState(fan, kCapMode,   gDirInst,    s); } // direction FORWARD

    home.onControl(onControl);  // register dispatch callback
    home.begin();               // build manifest + register cloud var/function; call once
}

void loop() {
    home.loop();
}
