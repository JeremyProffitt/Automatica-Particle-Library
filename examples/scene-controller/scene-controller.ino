// scene-controller.ino — automatica example
// =========================================
// Device archetype: a single SCENE ("movie night") — a one-shot trigger that runs a
// group of actions (dim lights, lower blinds, start the projector) and, optionally,
// reverts them.
//
// Concept (one per example): the SceneController interface, which is MOMENTARY.
// Unlike a light or blind, a scene has NO persisted reportable state: Alexa sends
// Activate or Deactivate and expects an acknowledgement, not a level. There is
// nothing to seed with setInitialState() and nothing to reportState().
//
// Alexa capability exposed:
//   - Alexa.SceneController (code 's', kCapScene) — SINGLETON, momentary.
//       cmd.boolVal carries the directive:  true = Activate, false = Deactivate.
//       Addressed by capability CODE (cmd.code); wire instance is kNoInstance (-1).
//
// Exact Alexa utterances this enables:
//   "Alexa, turn on movie night"   -> SceneController Activate   (boolVal=true)
//   "Alexa, turn off movie night"  -> SceneController Deactivate (boolVal=false)
//
// Hardware wiring assumption: none. runScene()/undoScene() are where you fan out to
// your other devices (relays, other endpoints, etc.).
#include "automatica.h"
#include "AutomaticaCloud.h"

using namespace automatica;

Automatica home;                 // facade. Never name it 'automatica' (namespace clash).
AutomaticaCloud cloud(home);     // Particle adapter; ctor calls home.setCloudPort(this).

// --- hardware stubs: replace with your scene apply / revert routines ---
static void runScene()  { /* TODO: dim lights, lower blinds, start projector */ }
static void undoScene() { /* TODO: restore the prior lighting/blind state */ }

// Control callback (SPEC §1.6). For the singleton SceneController dispatch on
// cmd.code; the only meaningful field is cmd.boolVal (true=Activate, false=Deactivate).
// There is no instance compare (singleton; cmd.instance == kNoInstance) and no state
// to apply — a scene is momentary. Return true = handled (the Lambda acks the scene);
// false -> the Lambda returns an Alexa error.
//
// Gotcha: CtlCommand is fully-qualified (automatica::CtlCommand) because the .ino
// preprocessor emits this prototype above `using namespace automatica;`.
static bool onControl(const automatica::CtlCommand& cmd, void*) {
    switch (cmd.code) {
        case kCapScene:
            if (cmd.boolVal) { runScene(); }   // Activate
            else             { undoScene(); }  // Deactivate
            return true;
    }
    return false;   // unknown/unhandled -> Lambda returns an Alexa error
}

void setup() {
    // Register the endpoint. {"SCENE_TRIGGER"} is the Alexa displayCategory that makes
    // Alexa treat this endpoint as an activatable scene rather than a device.
    int ep = home.addEndpoint("movie_night", "Movie Night", "automatica scene", {"SCENE_TRIGGER"});

    home.addScene(ep);   // 's' Alexa.SceneController — singleton, momentary.
                         // No setInitialState(): a momentary scene has no persisted state.

    home.onControl(onControl);   // register the control callback.
    home.begin();                // build manifest + register cloud variables/function. Call once.
}

void loop() {
    home.loop();   // drives the initial snapshot; a momentary scene has no state to publish.
}
