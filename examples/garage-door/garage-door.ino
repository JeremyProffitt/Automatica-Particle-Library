// garage-door.ino — automatica v2 example
// =============================================================================
// DEVICE ARCHETYPE: a garage door opener, modeled as a single INSTANCED
// ModeController with two positions and Alexa "open/close" semantics.
//
// ALEXA CAPABILITIES EXPOSED
//   'm' Alexa.ModeController, instance "Door.Position" (INSTANCED — addressed by
//        the instance index addMode() returns, here gDoorInst):
//        - modes: "Position.Open"  (spoken: "Open" / "Up")
//                 "Position.Closed" (spoken: "Closed" / "Down")
//        - ordered = false (these are discrete states, not a sequence, so Alexa
//          does NOT offer "raise/lower by one step").
//        - SEMANTICS attached so the friendly Open/Close verbs and the Alexa-app
//          Open/Closed state badge map onto the two mode values:
//            actionMappings: "Open"  -> SetMode "Position.Open"
//                            "Close" -> SetMode "Position.Closed"
//            stateMappings (kind 2 = StatesToValue(string)):
//                            "Open"   <- "Position.Open"
//                            "Closed" <- "Position.Closed"
//
// ALEXA UTTERANCES THIS ENABLES
//   "Alexa, open the garage door"   -> m, cmd.mode == "Position.Open"  (via Open semantic)
//   "Alexa, close the garage door"  -> m, cmd.mode == "Position.Closed" (via Close semantic)
//   "Alexa, set the garage door to up"    -> m, cmd.mode == "Position.Open"  (resource synonym)
//   "Alexa, is the garage door open?"     -> read-back from reported state mapping
//
// HARDWARE WIRING ASSUMPTION
//   applyDoor() is a stub. Wire it to pulse your opener relay toward the
//   requested position. No GPIO is touched in this example.
//
// GOTCHAS
//   - The {"GARAGE_DOOR"} display category makes Alexa REQUIRE a spoken security
//     PIN before it will execute the "open" action (a safety feature, not extra
//     code on the device).
//   - ModeController is an INSTANCED capability: an endpoint may carry several
//     mode instances, so the control callback MUST match on BOTH cmd.code == 'm'
//     AND cmd.instance == gDoorInst (the wire index addMode() returned). Do not
//     dispatch instanced caps by code alone.
//   - Facade object is named `home`, not `automatica`, to avoid clashing with
//     `namespace automatica`.
//   - This example dispatches by cmd.code without fully-qualifying CtlCommand in
//     a top-level prototype; the onControl signature itself fully-qualifies
//     automatica::CtlCommand for the .ino-generated forward prototype (which the
//     preprocessor inserts above `using namespace automatica;`).
// =============================================================================
#include "automatica.h"
#include "AutomaticaCloud.h"

using namespace automatica;

Automatica home;                 // facade; never name it 'automatica' (namespace clash)
AutomaticaCloud cloud(home);     // ctor wires Particle.variable/function/publish to the facade

int gDoorInst;                   // wire instance index of the "Door.Position" ModeController (set in setup)

// --- hardware stub: pulse the opener relay toward the requested position ---
// position is one of the declared mode values: "Position.Open" / "Position.Closed".
static void applyDoor(const std::string& position) { (void)position; }

// Control callback. Reads cmd.code (capability) and cmd.instance (which mode
// instance). Because ModeController is instanced, we require cmd.instance to
// equal gDoorInst before acting. cmd.mode carries the target mode value string.
// Returns true when handled; false otherwise (Lambda then returns an Alexa error).
static bool onControl(const automatica::CtlCommand& cmd, void*) {
    if (cmd.code == kCapMode && cmd.instance == gDoorInst) {  // 'm' + match the instance
        applyDoor(cmd.mode);   // cmd.mode: "Position.Open" or "Position.Closed"
        return true;
    }
    return false;              // not our capability/instance -> Alexa error
}

void setup() {
    // addEndpoint(id, friendlyName, description, displayCategories) -> Alexa idx.
    //   "garage" must match ^[a-z0-9_-]{1,24}$ and stay stable across reboots.
    //   {"GARAGE_DOOR"} triggers Alexa's PIN-protected open flow (see Gotchas).
    int garage = home.addEndpoint("garage", "Garage Door", "automatica garage door", {"GARAGE_DOOR"});

    // Build the ModeController config before adding it.
    ModeConfig door;
    door.ordered = false;   // discrete states, not a stepped sequence (no raise/lower-by-one)
    // Each ModeOption: .value is the wire/spec value; .resources are the spoken
    // synonyms Alexa accepts/announces for that value.
    { ModeOption m; m.value = "Position.Open";   m.resources.push_back("Open");   m.resources.push_back("Up");   door.modes.push_back(m); }
    { ModeOption m; m.value = "Position.Closed"; m.resources.push_back("Closed"); m.resources.push_back("Down"); door.modes.push_back(m); }
    door.resources.push_back("Position");   // friendly name(s) for the mode instance itself

    // Semantics (SPEC §1.7): bind Alexa's built-in Open/Close verbs and the
    // Open/Closed state badge to our two mode values.
    door.hasSemantics = true;
    // actionMappings: spoken action -> directive + target value. directive
    //   "SetMode" with valueStr = the mode value to set.
    { ActionMapping a; a.action = "Open";  a.directive = "SetMode"; a.valueStr = "Position.Open";   door.semantics.actionMappings.push_back(a); }
    { ActionMapping a; a.action = "Close"; a.directive = "SetMode"; a.valueStr = "Position.Closed"; door.semantics.actionMappings.push_back(a); }
    // stateMappings: kind 2 = StatesToValue(valueStr) — report this Alexa state
    //   name when the mode equals this string value.
    { StateMapping s; s.state = "Open";   s.kind = 2; s.valueStr = "Position.Open";   door.semantics.stateMappings.push_back(s); }
    { StateMapping s; s.state = "Closed"; s.kind = 2; s.valueStr = "Position.Closed"; door.semantics.stateMappings.push_back(s); }

    // addMode(idx, instanceName, cfg) -> wire instance index. Save it; the control
    // callback routes directives by matching cmd.instance against this value.
    gDoorInst = home.addMode(garage, "Door.Position", door);

    // Initial reported state: door Closed. For an INSTANCED cap, pass the instance
    // index (gDoorInst), not kNoInstance. CapState.mode holds the mode value.
    { CapState s; s.mode = "Position.Closed"; home.setInitialState(garage, kCapMode, gDoorInst, s); }

    home.onControl(onControl); // register the directive callback
    home.begin();              // build manifest + register cloud variable/function — call once
}

void loop() {
    home.loop();   // flush debounced state publishes (≤1/sec) + emit the initial snapshot
}
