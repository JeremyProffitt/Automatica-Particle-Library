// security-panel.ino — automatica example
// =======================================
// Device archetype: a single ALARM SECURITY PANEL on one Alexa endpoint.
//
// Concept (one per example): the SecurityPanelController interface, whose state is an
// ARM STATE expressed as a mode string (not a number or a bool). The selected arm
// state arrives in cmd.mode, and the panel's reported state is seeded as a mode
// string too (CapState.mode).
//
// Alexa capability exposed:
//   - Alexa.SecurityPanelController (code 'a', kCapSecurityPanel) — SINGLETON.
//       armState (cmd.mode), one of:
//         "ARMED_AWAY"   — armed, occupants away
//         "ARMED_STAY"   — armed, occupants home (perimeter only)
//         "ARMED_NIGHT"  — armed, night mode
//         "DISARMED"     — disarmed
//       Addressed by capability CODE (cmd.code); wire instance is kNoInstance (-1).
//
// Exact Alexa utterances this enables:
//   "Alexa, arm the alarm"            -> SecurityPanelController Arm    (mode="ARMED_AWAY")
//   "Alexa, arm the alarm in stay mode" -> Arm                         (mode="ARMED_STAY")
//   "Alexa, disarm the alarm"         -> SecurityPanelController Disarm (mode="DISARMED")
// (Disarm may require a PIN configured in the Alexa app, per Alexa's security rules.)
//
// Hardware wiring assumption: none in this stub — applyArmState() is where you drive
// your panel's relays / serial bus to the requested arm state.
#include "automatica.h"
#include "AutomaticaCloud.h"

using namespace automatica;

Automatica home;                 // facade. Never name it 'automatica' (namespace clash).
AutomaticaCloud cloud(home);     // Particle adapter; ctor calls home.setCloudPort(this).

// --- hardware stub: drive the alarm panel to the requested arm state ---
static void applyArmState(const std::string& armState) { (void)armState; }

// Control callback (SPEC §1.6). For the singleton SecurityPanelController dispatch on
// cmd.code; the arm state arrives as a STRING in cmd.mode (one of the four armState
// values above), not in boolVal/intVal. No instance compare (singleton;
// cmd.instance == kNoInstance). Return true = handled (library applies/reports the
// new arm state); false -> the Lambda returns an Alexa error.
//
// Gotcha: CtlCommand is fully-qualified (automatica::CtlCommand) because the .ino
// preprocessor emits this prototype above `using namespace automatica;`.
static bool onControl(const automatica::CtlCommand& cmd, void*) {
    switch (cmd.code) {
        case kCapSecurityPanel: applyArmState(cmd.mode); return true;
    }
    return false;   // unknown/unhandled -> Lambda returns an Alexa error
}

void setup() {
    // Register the endpoint. {"SECURITY_PANEL"} is the Alexa displayCategory that makes
    // Alexa treat this endpoint as a security panel (enabling arm/disarm phrasing).
    int ep = home.addEndpoint("alarm", "Alarm", "automatica security panel", {"SECURITY_PANEL"});

    home.addSecurityPanel(ep);   // 'a' Alexa.SecurityPanelController — singleton armState.

    // Seed initial reported state BEFORE begin(). SecurityPanelController stores its
    // value as a mode STRING, so populate CapState.mode (not .b/.i). For a singleton
    // pass the cap code with instance == kNoInstance.
    { CapState s; s.mode = "DISARMED"; home.setInitialState(ep, kCapSecurityPanel, kNoInstance, s); }

    home.onControl(onControl);   // register the control callback.
    home.begin();                // build manifest + register cloud variables/function. Call once.
}

void loop() {
    home.loop();   // drives debounced state publishes (≤1/sec) and the initial snapshot.
}
