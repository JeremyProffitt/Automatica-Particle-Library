// doorbell.ino — automatica example
// ============================================================================
// DEVICE ARCHETYPE: a smart doorbell with human-presence detection — one endpoint
// carrying two READ-ONLY, PROACTIVE event sources. Nothing here is controllable;
// the device only PUSHES state up via reportState() (sensors are reported, not
// controlled).
//
// ALEXA CAPABILITIES EXPOSED (both singletons, both proactive — they require the
// skill's async-event permission + PROACTIVE_REPORTING; no live proof until M3/CP-3):
//   'z' Alexa.DoorbellEventSource — a button press fires an Alexa DoorbellPress event.
//        Mechanism: set the bool state true, reportState(), then set it back false
//        — a momentary pulse. The Lambda consumer turns that publish into the
//        DoorbellPress proactive event. No control directive is ever received.
//   'x' Alexa.EventDetectionSensor — reports humanPresenceDetectionState as a bool
//        (true = DETECTED, false = NOT_DETECTED), e.g. from a PIR or camera person
//        detector. Reported on change; no control directive is ever received.
//
// EXACT ALEXA UTTERANCES / BEHAVIORS THIS ENABLES (event-driven, not spoken commands):
//   - Routines/Announcements triggered by "when the doorbell is pressed"
//     (e.g. "Alexa, announce when someone is at the front door").
//   - Routines triggered by "when a person is detected" at the Front Door endpoint.
//   (There are no Set/Adjust utterances — these interfaces are read-only.)
//
// HARDWARE WIRING ASSUMPTION: buttonPressed() reads a (debounced) doorbell button;
// personPresent() reads a PIR / camera person-detect line. Both are stubs returning
// false — wire them to your inputs.
//
// GOTCHAS:
//   - READ-ONLY sensors are REPORTED, not controlled: onControl() rejects every
//     directive (returns false), and you push values with setInitialState()+reportState().
//   - reportState() is coalesced and rate-limited to <=1 publish/sec per device
//     (kPublishMinIntervalMs). The doorbell pulse (true then false) relies on the
//     LATEST-WINS publish slot so the event consumer sees the press edge.
//   - Both caps are singletons => addressed by capability CODE (kCapDoorbell /
//     kCapEventDetection) with instance == kNoInstance.
//   - .ino preprocessor rule: auto-generated prototypes land ABOVE
//     'using namespace automatica;', so the callback parameter type must be
//     FULLY QUALIFIED (automatica::CtlCommand) for the generated prototype to compile.
// ============================================================================
#include "automatica.h"
#include "AutomaticaCloud.h"

using namespace automatica;

Automatica home;                 // the core facade; never name it 'automatica' (clashes with the namespace)
AutomaticaCloud cloud(home);     // Particle Device-OS adapter; its ctor calls home.setCloudPort(this)

// Endpoint index captured from addEndpoint(); used by reportState()/setInitialState()
// in loop() to push doorbell-press and presence updates.
int gEp;

// --- hardware stubs: replace with your button + presence sensor ---
static bool buttonPressed()  { return false; /* read the doorbell button (debounced) */ }
static bool personPresent()  { return false; /* read PIR / camera person-detect */ }

// Control callback: these interfaces are READ-ONLY, so reject every directive.
// Returning false makes the Lambda surface an Alexa error if anything ever targets
// these caps (Alexa should not, since the manifest declares them non-controllable).
// Fully-qualify CtlCommand (the .ino preprocessor emits the prototype before 'using namespace').
static bool onControl(const automatica::CtlCommand& cmd, void*) {
    (void)cmd;
    return false;
}

void setup() {
    // Register the endpoint. Returns the endpoint index (== Alexa endpoint idx);
    // declaration order is the stable device identity and must not change across reboots.
    //   id   "front_door"           — internal id, ^[a-z0-9_-]{1,24}$
    //   name "Front Door"           — Alexa friendlyName
    //   desc "automatica doorbell"  — Alexa description
    //   cat  {"OTHER"}              — Alexa displayCategory
    gEp = home.addEndpoint("front_door", "Front Door", "automatica doorbell", {"OTHER"});

    // Read-only, proactive singleton sensors (return value 0 ignored — addressed by code):
    home.addDoorbell(gEp);              // 'z' Alexa.DoorbellEventSource   (bool true => DoorbellPress)
    home.addEventDetectionSensor(gEp);  // 'x' Alexa.EventDetectionSensor  (bool true => human DETECTED)

    // Initial state: not pressed, no person detected. Singleton => CODE + kNoInstance;
    // CapState.b holds the bool. These seed the first manifest + snapshot.
    { CapState s; s.b = false; home.setInitialState(gEp, kCapDoorbell,       kNoInstance, s); }
    { CapState s; s.b = false; home.setInitialState(gEp, kCapEventDetection, kNoInstance, s); }

    home.onControl(onControl);   // register the (reject-all) callback; required even for read-only endpoints
    home.begin();                // build the manifest + register Particle var/function (call once)
}

void loop() {
    home.loop();                 // flush debounced state publishes + emit the initial snapshot

    // --- Doorbell: edge-detect the button and emit one DoorbellPress per press ---
    // On a rising edge (pressed now, not before), publish a momentary "pressed" pulse:
    // set the bool true + reportState() (the Lambda consumer turns this publish into a
    // DoorbellPress event), then immediately set it back false so the next press is a
    // fresh edge. setInitialState() here is just the supported way to mutate the cap's
    // current value before reportState() picks it up.
    static bool prevBtn = false;
    bool btn = buttonPressed();
    if (btn && !prevBtn) {
        { CapState s; s.b = true;  home.setInitialState(gEp, kCapDoorbell, kNoInstance, s); }
        home.reportState(gEp);                                  // -> queues a publish => DoorbellPress
        { CapState s; s.b = false; home.setInitialState(gEp, kCapDoorbell, kNoInstance, s); }
    }
    prevBtn = btn;

    // --- Presence: edge-detect human presence and report only on change ---
    // EventDetectionSensor is read-only; we push DETECTED/NOT_DETECTED via reportState()
    // whenever the sensor toggles. Reporting only on change avoids needless publishes
    // (reportState() is coalesced and rate-limited to <=1/sec anyway).
    static bool prevPresent = false;
    bool present = personPresent();
    if (present != prevPresent) {
        prevPresent = present;
        CapState s; s.b = present;                              // true => DETECTED
        home.setInitialState(gEp, kCapEventDetection, kNoInstance, s);
        home.reportState(gEp);                                  // -> queues a presence publish
    }
}
