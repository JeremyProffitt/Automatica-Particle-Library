// smart-switch.ino — automatica example: a WALL SWITCH (switched in-wall load).
//
// DEVICE ARCHETYPE: an in-wall switch driving a fixed load (porch light, fan,
// pump) through a relay or triac. One endpoint, one capability, on/off only.
// Identical capability shape to smart-plug; the only differences are the endpoint
// id/name and the SWITCH display category (so Alexa shows a switch, not an outlet).
//
// ALEXA CAPABILITIES EXPOSED:
//   'o' Alexa.PowerController — SINGLETON (one per endpoint, no instance name).
//       Carries a single bool: ON (true) / OFF (false). No range/units.
//
// ALEXA UTTERANCES this enables (friendlyName = "Porch Switch"):
//   "Alexa, turn on the porch switch"
//   "Alexa, turn off the porch switch"
//
// HARDWARE WIRING ASSUMPTION:
//   D7 drives the relay/triac controlling the switched load.
//   D7 is the on-board LED, handy for a wiring-free demo. HIGH = load ON.
//
// GOTCHAS:
//   - PowerController is a SINGLETON: addressed in the callback and in
//     setInitialState() by its CODE (kCapPower) with the kNoInstance sentinel,
//     not by an instance index. addPower() returns 0, unused for singletons.
//   - Do not name the Automatica object 'automatica' (namespace clash). Use `home`.
#include "automatica.h"
#include "AutomaticaCloud.h"

using namespace automatica;

Automatica home;                 // the facade: holds endpoints, manifest, callback.
AutomaticaCloud cloud(home);     // ctor calls home.setCloudPort(this) — wires Particle.

// --- hardware stub: drive the relay/triac controlling the switched load ---
// Replace with your real GPIO. on==true energizes the relay/triac (load ON).
static void applyPower(bool on) { digitalWrite(D7, on ? HIGH : LOW); }

// Control callback: invoked after the library decodes + validates an inbound
// automaticaCtl command (Ascii85 -> binary -> CtlCommand).
//   - cmd.code    : capability code driven; here only kCapPower ('o').
//   - cmd.boolVal : PowerController state (true = ON, false = OFF).
//   - cmd.instance: kNoInstance for singletons (not read here).
// Dispatch on cmd.code; return true == handled (success reported to Alexa);
// return false == unhandled -> Lambda returns an Alexa error.
static bool onControl(const automatica::CtlCommand& cmd, void*) {
    switch (cmd.code) {
        case kCapPower: applyPower(cmd.boolVal); return true;   // ON/OFF the load
    }
    return false;   // unknown/unhandled code -> Alexa error
}

void setup() {
    pinMode(D7, OUTPUT);

    // Register the endpoint. Args: stable id (^[a-z0-9_-]{1,24}$, part of device
    // identity — must be stable across reboots), Alexa friendlyName, description,
    // displayCategories ({"SWITCH"} makes Alexa render a switch). Returns the
    // 0-based endpoint index used below.
    int sw = home.addEndpoint("porch_switch", "Porch Switch", "automatica switch", {"SWITCH"});

    // Add the singleton PowerController ('o'). Return value (0) is unused for singletons.
    home.addPower(sw);

    // Initial state (BEFORE begin()): power OFF. Singleton -> pass kCapPower +
    // kNoInstance. CapState.b carries the PowerController bool. Seeds the first
    // state snapshot reported to Alexa.
    { CapState s; s.b = false; home.setInitialState(sw, kCapPower, kNoInstance, s); }

    home.onControl(onControl);   // register the control callback (no user ctx)
    home.begin();                // build manifest + register Particle var/function. Call once.
}

void loop() {
    home.loop();   // flush debounced state publishes (<=1/sec) + emit initial snapshot.
}
