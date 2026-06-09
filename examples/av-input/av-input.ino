// =============================================================================
// av-input.ino — automatica example
// Device archetype: AV receiver / TV input switcher (one endpoint, display
// category "TV"). One clearly-scoped concept: switching the active source input.
//
// ALEXA CAPABILITIES EXPOSED (both SINGLETON — one per endpoint, no instance):
//   • Alexa.PowerController   (code 'o', kCapPower) — TurnOn / TurnOff (bool).
//   • Alexa.InputController    (code 'j', kCapInput) — SelectInput. The WIRE
//     carries the chosen input's INDEX (int) into the canonical list the Lambda
//     advertises (SPEC §1.6 / capabilities.md). There is NO range/unit — the
//     value is an array index, not a measurement:
//       0=HDMI 1  1=HDMI 2  2=HDMI 3  3=HDMI 4  4=TV  5=AUX 1  6=CD  7=DVD
//     cmd.intVal is that index; map it to your physical input. The order MUST
//     match the Lambda's device.InputNames (see kInputs[] below).
//
// ALEXA UTTERANCES THIS ENABLES:
//   "Alexa, turn on the receiver"
//   "Alexa, turn off the receiver"
//   "Alexa, set the receiver input to HDMI 2"
//   "Alexa, change the receiver to TV"
//
// HARDWARE WIRING ASSUMPTION:
//   D7 drives the receiver power (HIGH = on). The on-board LED on most Particle
//   devices is wired to D7, so power state is visible without extra wiring.
//   Input selection (selectInput) is a stub — wire it to your IR / serial blaster.
//
// GOTCHAS:
//   • Both caps here are SINGLETONS: addressed by cmd.code in the callback (NOT
//     by instance). cmd.instance is kNoInstance for singletons — do not branch on it.
//   • InputController stores an INDEX, not a level — never treat cmd.intVal as a
//     percentage or a measured value.
//   • .ino preprocessor prototype rule: the Arduino/Particle preprocessor inserts
//     an auto-prototype for onControl ABOVE the `using namespace automatica;`
//     line, so the parameter type must be fully qualified as automatica::CtlCommand
//     for the auto-prototype to match the definition. Keep it fully qualified.
// =============================================================================
#include "automatica.h"
#include "AutomaticaCloud.h"

using namespace automatica;

// The Automatica core facade. NOTE: never name this variable 'automatica' — that
// collides with the `automatica` namespace. AutomaticaCloud's ctor calls
// home.setCloudPort(this), wiring Particle.variable/.function/.publish to the core.
Automatica home;                 // never name it 'automatica' (namespace clash)
AutomaticaCloud cloud(home);

// The canonical input order. The position of each name IS the wire index Alexa
// sends in cmd.intVal (HDMI 1 -> 0, DVD -> 7). This list MUST match the Lambda's
// device.InputNames exactly, or indices will resolve to the wrong physical input.
static const char* kInputs[] = {"HDMI 1","HDMI 2","HDMI 3","HDMI 4","TV","AUX 1","CD","DVD"};

// --- hardware stubs: replace with your IR / serial input-select control ---
static void applyPower(bool on)      { digitalWrite(D7, on ? HIGH : LOW); } // HIGH = receiver on
static void selectInput(int index)   { /* drive the receiver to kInputs[index] */ (void)index; } // index 0..7

// Control callback. Invoked once per validated directive. Reads:
//   cmd.code     — which capability is being controlled (we dispatch on this).
//   cmd.boolVal  — for 'o' PowerController: true=TurnOn, false=TurnOff.
//   cmd.intVal   — for 'j' InputController: the SelectInput INDEX into kInputs[].
//   cmd.instance — kNoInstance here (both caps are singletons); not read.
// Return true = "handled" (the facade applies/persists state and reports it).
// Return false = unhandled -> the Lambda returns an Alexa error to the user.
// Fully-qualify CtlCommand (the .ino preprocessor emits the prototype before 'using namespace').
static bool onControl(const automatica::CtlCommand& cmd, void*) {
    switch (cmd.code) {
        case kCapPower: applyPower(cmd.boolVal); return true;  // 'o' singleton
        case kCapInput: selectInput(cmd.intVal); return true;  // 'j' singleton; intVal = input index
    }
    return false;   // unknown/unhandled -> Lambda returns an Alexa error
}

void setup() {
    pinMode(D7, OUTPUT);

    // Register the endpoint. Returns the endpoint index (== Alexa idx); the order
    // of addEndpoint calls is the stable device identity and must not change
    // across reboots. Args: id (^[a-z0-9_-]{1,24}$), friendlyName, description, displayCategories.
    int avr = home.addEndpoint("av_receiver", "AV Receiver", "automatica input switcher", {"TV"});

    // Declare the two SINGLETON capabilities on this endpoint. Singleton builders
    // return 0 (no instance index to keep) — they are addressed by code, not instance.
    home.addPower(avr);   // 'o' PowerController: TurnOn/TurnOff
    home.addInput(avr);   // 'j' InputController: SelectInput (stores the chosen input INDEX)

    // Initial state, applied before begin() so the first manifest + snapshot are correct.
    // For singletons pass the capability code with instance = kNoInstance.
    //   CapState.b -> bool field (PowerController): start powered off.
    //   CapState.i -> int field (InputController): start on input index 0 = HDMI 1.
    { CapState s; s.b = false; home.setInitialState(avr, kCapPower, kNoInstance, s); }
    { CapState s; s.i = 0;     home.setInitialState(avr, kCapInput, kNoInstance, s); } // index 0 = HDMI 1

    home.onControl(onControl);  // register the dispatch callback (ctx unused)
    home.begin();               // build manifest + register cloud var/function; call once
}

void loop() {
    home.loop();
}
