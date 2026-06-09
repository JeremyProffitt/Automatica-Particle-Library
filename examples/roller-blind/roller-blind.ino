// roller-blind.ino — automatica v2 example
// ========================================
// Device archetype: a single motorized ROLLER BLIND on one Alexa endpoint.
//
// Concept (one per example): an INSTANCED RangeController with SEMANTICS. A
// RangeController ('r', kCapRange) is a generic numeric slider; semantics layer
// natural action verbs (Open/Close/Raise/Lower) on top so the user need not say
// "set position to …". Because RangeController is INSTANCED, the endpoint may carry
// several, each with its own instance NAME ("Blind.Position" here) and a compact
// wire instance INDEX assigned in declaration order. The callback routes on that
// index (cmd.instance), not on cmd.code alone.
//
// Alexa capability exposed:
//   - Alexa.RangeController (code 'r') instance "Blind.Position"
//       range 0..100, precision 1, unit "Percent"  (0 = fully closed, 100 = fully open)
//       Action semantics  (utterance verb -> range directive):
//         Open  -> SetRangeValue 100      Close -> SetRangeValue 0
//         Raise -> AdjustRangeValue +10    Lower -> AdjustRangeValue -10
//       State semantics   (numeric value -> app label):
//         value 0      -> "Closed"        value 1..100 -> "Open"
//
// Exact Alexa utterances this enables:
//   "Alexa, set the roller blind position to 60%"  -> RangeController SetRangeValue    (intVal=60)
//   "Alexa, open the blind"                        -> SetRangeValue 100  (intVal=100)
//   "Alexa, close the blind"                       -> SetRangeValue 0    (intVal=0)
//   "Alexa, raise the blind"                       -> AdjustRangeValue +10
//   "Alexa, lower the blind"                       -> AdjustRangeValue -10
// (The Lambda resolves AdjustRangeValue against last-known state and delivers an
//  absolute, clamped 0..100 value in cmd.intVal, so the sketch only applies absolutes.)
//
// Hardware wiring assumption: none in this stub — applyPosition() is where you drive
// your motor controller toward the target percent (add limit switches / position
// feedback as needed).
#include "automatica.h"
#include "AutomaticaCloud.h"

using namespace automatica;

Automatica home;                 // facade. Never name it 'automatica' (namespace clash).
AutomaticaCloud cloud(home);     // Particle adapter; ctor calls home.setCloudPort(this).

// Saved wire instance index of the "Blind.Position" RangeController, returned by
// addRange() in setup() and compared against cmd.instance in the callback to route
// control. (With one range this is 0, but always route by the saved index so adding
// more instanced caps later stays correct.)
int gPositionInst;

// --- hardware stub: drive the motor toward `pct` (0=closed, 100=open) ---
static void applyPosition(int pct) { (void)pct; }

// Control callback (SPEC §1.6). For an instanced RangeController, dispatch on BOTH
// the capability code AND the instance index:
//   - cmd.code     : 'r' (kCapRange) for any RangeController directive.
//   - cmd.instance : the wire index identifying WHICH range — match gPositionInst.
//   - cmd.intVal   : the absolute, clamped 0..100 target position.
// Return true = handled; false -> Lambda returns an Alexa error.
//
// Gotcha: CtlCommand is fully-qualified (automatica::CtlCommand) because the .ino
// preprocessor emits this prototype above `using namespace automatica;`.
static bool onControl(const automatica::CtlCommand& cmd, void*) {
    if (cmd.code == kCapRange && cmd.instance == gPositionInst) {
        applyPosition(cmd.intVal);
        return true;
    }
    return false;
}

void setup() {
    // Register the endpoint. {"INTERIOR_BLIND"} is the Alexa displayCategory that
    // gives the blind its icon and groups it with window coverings.
    int blind = home.addEndpoint("blind", "Roller Blind", "automatica roller blind", {"INTERIOR_BLIND"});

    // Configure the RangeController before adding it. RangeConfig fields (SPEC §1.7):
    RangeConfig pos;
    pos.min = 0; pos.max = 100;        // inclusive value bounds for this range.
    pos.precision = 1;                 // step granularity for Adjust* directives.
    pos.unit = "Percent";             // Alexa unit-of-measure catalog name ("" = unitless).
    pos.resources.push_back("Position"); // friendly names Alexa accepts for this range
    pos.resources.push_back("Height");   // ("set the blind HEIGHT to 50%", etc.).

    // Semantics: map spoken action verbs to range directives (actionMappings) and map
    // numeric values back to app state labels (stateMappings). Set hasSemantics so the
    // manifest emits the semantics block.
    pos.hasSemantics = true;
    // actionMappings — directive is "SetRangeValue" (absolute, uses .value) or
    // "AdjustRangeValue" (relative delta, uses .value):
    { ActionMapping a; a.action = "Open";  a.directive = "SetRangeValue";    a.value = 100; pos.semantics.actionMappings.push_back(a); }
    { ActionMapping a; a.action = "Close"; a.directive = "SetRangeValue";    a.value = 0;   pos.semantics.actionMappings.push_back(a); }
    { ActionMapping a; a.action = "Lower"; a.directive = "AdjustRangeValue"; a.value = -10; pos.semantics.actionMappings.push_back(a); }
    { ActionMapping a; a.action = "Raise"; a.directive = "AdjustRangeValue"; a.value = 10;  pos.semantics.actionMappings.push_back(a); }
    // stateMappings — kind 0 = StatesToValue(single value a); kind 1 = StatesToRange(a..b):
    { StateMapping s; s.state = "Closed"; s.kind = 0; s.a = 0;            pos.semantics.stateMappings.push_back(s); }  // exactly 0 -> "Closed"
    { StateMapping s; s.state = "Open";   s.kind = 1; s.a = 1; s.b = 100; pos.semantics.stateMappings.push_back(s); }  // 1..100 -> "Open"

    // Add the instanced RangeController; "Blind.Position" is its Alexa instance NAME.
    // addRange returns the wire instance INDEX — save it to route control later.
    gPositionInst = home.addRange(blind, "Blind.Position", pos);

    // Seed initial reported state BEFORE begin(). For an INSTANCED cap, pass the wire
    // instance index (gPositionInst), not kNoInstance. RangeController uses CapState.i.
    { CapState s; s.i = 100; home.setInitialState(blind, kCapRange, gPositionInst, s); }  // start fully open

    home.onControl(onControl);   // register the control callback.
    home.begin();                // build manifest + register cloud variables/function. Call once.
}

void loop() {
    home.loop();   // drives debounced state publishes (≤1/sec) and the initial snapshot.
}
