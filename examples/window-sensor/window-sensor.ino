// window-sensor.ino — automatica example: a READ-ONLY window contact sensor.
//
// DEVICE ARCHETYPE
//   A single Alexa endpoint in the CONTACT_SENSOR display category that reports
//   whether a window is open or closed, driven by a magnetic reed switch.
//
// ALEXA CAPABILITIES EXPOSED
//   - 'd' Alexa.ContactSensor  (singleton, instance == kNoInstance)  [READ-ONLY]
//       State is a single bool (CapState.b):  true = DETECTED = window OPEN
//       (reed contacts apart),  false = NOT_DETECTED = window CLOSED.
//       Contact sensors are *reported*, never *controlled*: the device pushes
//       state changes up to Alexa; there are no inbound directives.
//
// ALEXA UTTERANCES THIS ENABLES
//   "Alexa, is the window open?"
//   "Alexa, is the window closed?"
//   ...plus routines/announcements triggered "When the window opens/closes".
//   (There is NO "open the window" / "close the window" — a contact sensor is
//    read-only and cannot be commanded.)
//
// HARDWARE WIRING ASSUMPTION
//   A reed switch wired to a GPIO using INPUT_PULLUP (one leg to the pin, the
//   other to GND). With a normally-open reed: closed window keeps the magnet
//   near the switch (contacts closed -> pin reads LOW), open window separates
//   them (contacts open -> pull-up makes the pin read HIGH). readReed() below
//   is a stub; map your real wiring so it returns true when the window is OPEN.
//
// GOTCHAS
//   - SINGLETON capability: addContactSensor() is a singleton, so it is addressed
//     by its code (kCapContactSensor) with instance == kNoInstance everywhere
//     (setInitialState here); the builder's return value is not used for routing.
//   - reportState() vs control: sensors use reportState() to publish a fresh
//     snapshot to the cloud (latest-wins, coalesced, <=1 publish/sec). They never
//     accept directives, so onControl() always returns false.
//   - .ino preprocessor: readReed() and onControl() are defined before they are
//     used (and are static), so no forward prototype is needed here. If you move a
//     function below its first use, add an explicit prototype that exactly matches
//     the definition — the Arduino auto-prototype generator can mis-order them.
#include "automatica.h"
#include "AutomaticaCloud.h"

using namespace automatica;

Automatica home;                 // the core facade. Never name it 'automatica'
                                 // (that collides with the namespace).
AutomaticaCloud cloud(home);     // Particle adapter; its ctor calls
                                 // home.setCloudPort(this) to wire Particle.variable
                                 // / Particle.function / Particle.publish.

int gWindowEp;                   // endpoint index returned by addEndpoint(); used
                                 // to address this endpoint in every later call.

// --- hardware stub: read the reed switch (true = window open / contacts apart) ---
// Replace with a digitalRead() on your reed-switch pin (use INPUT_PULLUP wiring).
// Return true when the window is OPEN (DETECTED), false when CLOSED (NOT_DETECTED).
static bool readReed() { return false; }

// Control callback. Contact sensors are READ-ONLY, so there is nothing to dispatch:
// we read no CtlCommand fields (cmd.code/instance/boolVal/... are all ignored) and
// return false. Returning false signals "not handled" -> the Lambda surfaces an
// Alexa error for any directive aimed at this endpoint, which is the correct
// behavior for a sensor that cannot be commanded. (Compare: a controllable device
// would switch on cmd.code, act, and return true to mean "handled".)
static bool onControl(const automatica::CtlCommand& cmd, void*) {
    (void)cmd;
    return false;   // read-only sensor -> Lambda returns an Alexa error for any directive
}

void setup() {
    // Register the endpoint. Args: stable id (^[a-z0-9_-]{1,24}$, part of the
    // device identity — keep it constant across reboots), Alexa friendlyName,
    // description, and displayCategories. CONTACT_SENSOR makes Alexa render it as
    // a window/door open-close sensor. Returns the endpoint index used below.
    gWindowEp = home.addEndpoint("window", "Window", "automatica window sensor", {"CONTACT_SENSOR"});

    // Declare the read-only contact-sensor capability ('d') on this endpoint.
    // Singleton: addressed by code + kNoInstance, so the return value is unused.
    home.addContactSensor(gWindowEp);

    // Seed the initial reported state from the current hardware reading so the
    // first cloud snapshot reflects reality. CapState.b is the contact bool
    // (true = OPEN/DETECTED). For a singleton, pass the code (kCapContactSensor)
    // and instance == kNoInstance.
    { CapState s; s.b = readReed(); home.setInitialState(gWindowEp, kCapContactSensor, kNoInstance, s); }

    // Attach the control callback. Required even for read-only endpoints; here it
    // simply rejects every directive (returns false).
    home.onControl(onControl);

    // Build the manifest and register the Particle cloud variables/function.
    // Call once, after all endpoints/capabilities are declared.
    home.begin();
}

void loop() {
    // Drive the core every loop: flushes debounced publishes and emits the first
    // state snapshot once connected.
    home.loop();

    // Poll the reed switch; on a CHANGE, update the stored state and proactively
    // report it to the cloud. reportState() marks the endpoint dirty; the next
    // home.loop() publishes the coalesced, latest-wins snapshot (<=1/sec). This is
    // the report path for a sensor — there is no control path.
    static bool prevOpen = false;
    bool open = readReed();
    if (open != prevOpen) {
        prevOpen = open;
        // Update the capability's current state to the new reading (same singleton
        // addressing as the initial seed: code + kNoInstance).
        { CapState s; s.b = open; home.setInitialState(gWindowEp, kCapContactSensor, kNoInstance, s); }
        home.reportState(gWindowEp);   // queue a proactive state report to Alexa
    }
}
