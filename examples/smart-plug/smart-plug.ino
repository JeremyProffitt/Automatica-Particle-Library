// smart-plug.ino — automatica example: a SMART PLUG (switched mains outlet).
//
// DEVICE ARCHETYPE: a single relay-controlled outlet (e.g. a coffee maker plugged
// into a controllable socket). One endpoint, one capability, on/off only.
//
// ALEXA CAPABILITIES EXPOSED:
//   'o' Alexa.PowerController — SINGLETON (one per endpoint, no instance name).
//       Carries a single bool: ON (true) / OFF (false). No range/units.
//
// ALEXA UTTERANCES this enables (friendlyName = "Coffee Maker"):
//   "Alexa, turn on the coffee maker"
//   "Alexa, turn off the coffee maker"
//
// HARDWARE WIRING ASSUMPTION:
//   D7 drives the relay coil that switches mains power to the outlet.
//   D7 is the Photon/Argon/Boron on-board LED, handy for a wiring-free demo.
//   HIGH = relay energized = outlet powered.
//
// GOTCHAS:
//   - PowerController is a SINGLETON capability: it is addressed in the control
//     callback and in setInitialState() by its CODE (kCapPower) with the
//     kNoInstance sentinel — NOT by an instance index. addPower() returns 0,
//     which is unused for singletons.
//   - Do not name the Automatica object 'automatica' — it would clash with the
//     `automatica` namespace. We use `home`.
#include "automatica.h"
#include "AutomaticaCloud.h"

using namespace automatica;

Automatica home;                 // the facade: holds endpoints, manifest, callback.
AutomaticaCloud cloud(home);     // ctor calls home.setCloudPort(this) — wires Particle.

// --- hardware stub: drive the relay switching mains power to the outlet ---
// Replace with your real relay GPIO. on==true energizes the relay (outlet ON).
static void applyPower(bool on) { digitalWrite(D7, on ? HIGH : LOW); }

// Control callback: invoked by the library after it decodes + validates an
// inbound automaticaCtl command from the cloud (Ascii85 -> binary -> CtlCommand).
//   - cmd.code     : the capability code that was driven; here only kCapPower ('o').
//   - cmd.boolVal  : for PowerController, the requested power state (true = ON).
//   - cmd.instance : kNoInstance for singletons like PowerController (not read here).
// Dispatch on cmd.code; return true == "handled" (Lambda reports success to Alexa).
// Returning false signals unhandled -> the Lambda returns an Alexa error.
static bool onControl(const automatica::CtlCommand& cmd, void*) {
    switch (cmd.code) {
        case kCapPower: applyPower(cmd.boolVal); return true;   // ON/OFF the outlet
    }
    return false;   // unknown/unhandled code -> Alexa error
}

void setup() {
    pinMode(D7, OUTPUT);

    // Register the endpoint. Args: stable id (^[a-z0-9_-]{1,24}$, part of the device
    // identity — must NOT change across reboots), Alexa friendlyName, description,
    // and displayCategories. SMARTPLUG is the display category here; "OUTLET" is an
    // equally valid alternative. Returns the endpoint index (0-based) used below.
    int plug = home.addEndpoint("coffee_maker", "Coffee Maker", "automatica plug", {"SMARTPLUG"});

    // Add the singleton PowerController ('o') to this endpoint. Return value (0) is
    // unused — singletons are routed by code, not instance.
    home.addPower(plug);

    // Initial state (must be set BEFORE begin()): power OFF. For a singleton, pass the
    // capability code (kCapPower) + the kNoInstance sentinel. CapState.b carries the
    // bool for PowerController. This seeds the first state snapshot reported to Alexa.
    { CapState s; s.b = false; home.setInitialState(plug, kCapPower, kNoInstance, s); }

    home.onControl(onControl);   // register the control callback (no user ctx here)
    home.begin();                // build manifest + register Particle var/function. Call once.
}

void loop() {
    home.loop();   // flush debounced state publishes (<=1/sec) + emit initial snapshot.
}
