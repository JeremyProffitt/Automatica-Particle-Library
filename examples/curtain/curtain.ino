// curtain.ino — automatica example
// ============================================================================
// DEVICE ARCHETYPE: a motorized window curtain / shade whose open percentage is
// driven by a positional RangeController.
//
// ALEXA CAPABILITIES EXPOSED (one cleanly-scoped concept: a single ranged axis):
//   'r' Alexa.RangeController
//        - instance string: "Curtain.Position"  (the named, instanced axis)
//        - range:     min 0 .. max 100, precision 1
//        - unit:      "Percent"   (catalog unit; rendered as "%" in the app)
//        - semantics: Open/Close/Raise/Lower utterances mapped to range directives
//        - control:   cmd.intVal carries the target/adjusted position
//
// EXACT ALEXA UTTERANCES THIS ENABLES:
//   "Alexa, open the curtains"        -> SetRangeValue 100      (Open  action)
//   "Alexa, close the curtains"       -> SetRangeValue 0        (Close action)
//   "Alexa, raise the curtains"       -> AdjustRangeValue +10   (Raise action)
//   "Alexa, lower the curtains"       -> AdjustRangeValue -10   (Lower action)
//   "Alexa, set the curtains to 50%"  -> SetRangeValue 50       (direct range)
//   "Alexa, what's the curtain position?"  (reads back the reported range value)
// State semantics also report the curtain to the Alexa app as Closed (value 0)
// or Open (value 1..100).
//
// HARDWARE WIRING ASSUMPTION: applyPosition() below is a stub. Wire it to your
// motor driver (H-bridge / stepper / servo) so 0 = fully closed, 100 = fully open.
//
// GOTCHAS:
//   - RangeController is an INSTANCED capability: addRange() returns a wire
//     instance index that MUST be saved (gPositionInst) and matched in the
//     control callback via cmd.instance. Contrast with singletons (power/lock)
//     which carry cmd.instance == kNoInstance and are routed by cmd.code alone.
//   - .ino preprocessor rule: the Arduino/Particle build auto-generates function
//     prototypes and inserts them ABOVE the 'using namespace automatica;' line,
//     so any type in a callback signature must be FULLY QUALIFIED
//     (automatica::CtlCommand) or the generated prototype won't compile.
// ============================================================================
#include "automatica.h"
#include "AutomaticaCloud.h"

using namespace automatica;

Automatica home;                 // the core facade; never name it 'automatica' (clashes with the namespace)
AutomaticaCloud cloud(home);     // Particle Device-OS adapter; its ctor calls home.setCloudPort(this)

// Wire instance index for the "Curtain.Position" RangeController, captured from
// addRange() in setup() and compared against cmd.instance in onControl() to route
// directives to the right axis (this endpoint has only one, but the pattern scales).
int gPositionInst;

// --- hardware stub: drive the motor toward `pct` (0 = closed, 100 = open) ---
// Replace the body with your motor-control code; signature/behavior must not change.
static void applyPosition(int pct) { (void)pct; }

// Control callback: invoked once per validated inbound directive. Reads:
//   cmd.code     — capability code; kCapRange ('r') for this endpoint
//   cmd.instance — wire instance index of the targeted RangeController
//   cmd.intVal   — resolved target position 0..100 (Set or Adjust already applied)
// Return true  => handled (Lambda reports success to Alexa).
// Return false => unhandled (Lambda returns an Alexa error to the user).
// Fully-qualify CtlCommand (the .ino preprocessor emits the prototype before 'using namespace').
static bool onControl(const automatica::CtlCommand& cmd, void*) {
    switch (cmd.code) {
        case kCapRange:
            // Route by instance: only act on our Curtain.Position axis.
            if (cmd.instance == gPositionInst) { applyPosition(cmd.intVal); return true; }
            break;
    }
    return false;   // unknown/unhandled -> Lambda returns an Alexa error
}

void setup() {
    // Register the endpoint. Returns the endpoint index (== Alexa endpoint idx);
    // declaration order is the stable device identity and must not change across reboots.
    //   id   "curtain"            — internal id, ^[a-z0-9_-]{1,24}$
    //   name "Curtain"            — Alexa friendlyName (what the user says)
    //   desc "automatica curtain" — Alexa description
    //   cat  {"CURTAIN"}          — Alexa displayCategory (drives the app icon/affordances)
    int ep = home.addEndpoint("curtain", "Curtain", "automatica curtain", {"CURTAIN"});

    // Configure the positional range BEFORE adding it. RangeConfig fields:
    //   min/max     — inclusive value bounds (0..100 here)
    //   precision   — step granularity for Adjust* directives (1 => whole percents)
    //   unit        — Alexa unit catalog name ("Percent" renders as "%")
    //   resources   — asset/friendly names for this range ("Position", "Opening"),
    //                 letting Alexa match phrasing like "set the curtain position to..."
    RangeConfig pos;
    pos.min = 0; pos.max = 100; pos.precision = 1; pos.unit = "Percent";
    pos.resources.push_back("Position");
    pos.resources.push_back("Opening");

    // Semantics: map natural Open/Close/Raise/Lower phrasing to concrete range directives.
    // hasSemantics=true tells the manifest builder to emit the actionMappings/stateMappings below.
    pos.hasSemantics = true;
    // actionMappings — utterance "action" -> directive + value:
    //   directive "SetRangeValue"    sets an absolute position (a.value)
    //   directive "AdjustRangeValue" applies a signed delta    (a.value)
    { ActionMapping a; a.action = "Open";  a.directive = "SetRangeValue";    a.value = 100; pos.semantics.actionMappings.push_back(a); }  // "open"  -> 100
    { ActionMapping a; a.action = "Close"; a.directive = "SetRangeValue";    a.value = 0;   pos.semantics.actionMappings.push_back(a); }  // "close" -> 0
    { ActionMapping a; a.action = "Raise"; a.directive = "AdjustRangeValue"; a.value = 10;  pos.semantics.actionMappings.push_back(a); }  // "raise" -> +10
    { ActionMapping a; a.action = "Lower"; a.directive = "AdjustRangeValue"; a.value = -10; pos.semantics.actionMappings.push_back(a); }  // "lower" -> -10
    // stateMappings — report range value as a friendly app state:
    //   kind 0 StatesToValue(a)        : exact value a maps to this state ("Closed" when 0)
    //   kind 1 StatesToRange(a..b)     : any value in [a,b] maps to this state ("Open" when 1..100)
    { StateMapping s; s.state = "Closed"; s.kind = 0; s.a = 0;            pos.semantics.stateMappings.push_back(s); }
    { StateMapping s; s.state = "Open";   s.kind = 1; s.a = 1; s.b = 100; pos.semantics.stateMappings.push_back(s); }

    // Add the instanced RangeController. The returned wire instance index identifies
    // this axis in both inbound control (cmd.instance) and setInitialState below.
    gPositionInst = home.addRange(ep, "Curtain.Position", pos);

    // Initial state: position 0 (fully closed). For an instanced cap, pass the
    // SAME wire instance index returned by addRange(); CapState.i holds the int value.
    { CapState s; s.i = 0; home.setInitialState(ep, kCapRange, gPositionInst, s); }

    home.onControl(onControl);   // register the inbound-directive callback
    home.begin();                // build the manifest + register Particle var/function (call once)
}

void loop() { home.loop(); }     // flush debounced state publishes + emit the initial snapshot
