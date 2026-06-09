// media-player.ino — automatica example
// =============================================================================
// DEVICE ARCHETYPE: a TV / media player. One Alexa endpoint (displayCategory
// "TV") that combines transport controls with a volume/mute speaker.
//
// ALEXA CAPABILITIES EXPOSED (both are SINGLETON capabilities — one per endpoint,
// addressed in the callback by cmd.code, never by an instance index):
//   'y'  addPlayback()  -> Alexa.PlaybackController (MOMENTARY)
//          - The directive arrives as a transport op number in cmd.intVal
//            (see the OP_* enum below: 1=Play .. 8=StartOver).
//          - Momentary = there is NO persisted state. Nothing to report back;
//            you just fire the action (IR/CEC pulse, etc.) and return true.
//   'u'  addSpeaker()   -> Alexa.Speaker
//          - cmd.intVal  : target volume, integer 0..100 (no unit suffix)
//          - cmd.boolVal : muted flag (true = muted)
//          - Has persisted state (VAL_SPEAKER): CapState.i = volume 0..100,
//            CapState.b = muted. We seed it in setup() via setInitialState().
//
// ALEXA UTTERANCES THIS ENABLES:
//   "Alexa, pause the TV"              -> Playback op 2 (Pause)
//   "Alexa, play the TV"              -> Playback op 1 (Play)
//   "Alexa, next on the TV"           -> Playback op 4 (Next)
//   "Alexa, previous on the TV"       -> Playback op 5 (Previous)
//   "Alexa, set the TV volume to 15"  -> Speaker volume (cmd.intVal = 15)
//   "Alexa, mute the TV"              -> Speaker muted (cmd.boolVal = true)
//
// HARDWARE WIRING ASSUMPTION: none specific — the apply*() stubs below are where
// you wire your real IR blaster / HDMI-CEC bridge / serial transport + volume.
//
// GOTCHAS:
//   - .ino prototype rule: the Arduino preprocessor auto-generates a forward
//     prototype for onControl() and inserts it at the TOP of the file, ABOVE the
//     `using namespace automatica;` line. To keep that generated prototype
//     compiling, onControl()'s signature spells the type out as
//     `automatica::CtlCommand` rather than the unqualified `CtlCommand`.
//   - Playback is momentary: never call setInitialState()/reportState() for it.
//   - Speaker is a singleton: in the callback match cmd.code == kCapSpeaker; do
//     not look at cmd.instance (it is kNoInstance for singletons).
// =============================================================================
#include "automatica.h"
#include "AutomaticaCloud.h"

using namespace automatica;

Automatica home;                 // the core facade. Never name it 'automatica' (clashes with the namespace).
AutomaticaCloud cloud(home);     // Particle adapter; its ctor calls home.setCloudPort(this).

// Playback op enum carried in cmd.intVal for the 'y' PlaybackController
// (momentary; there is no state to report back). The numbers are the wire op
// codes the Lambda sends for each Alexa transport directive:
//   1 Play  2 Pause  3 Stop  4 Next  5 Previous  6 Rewind  7 FastForward  8 StartOver
enum { OP_PLAY = 1, OP_PAUSE, OP_STOP, OP_NEXT, OP_PREVIOUS, OP_REWIND, OP_FASTFORWARD, OP_STARTOVER };

// --- hardware stubs: replace the bodies with your IR/CEC/transport + volume lines ---
static void applyTransport(int op) { /* emit IR/CEC for the transport op (one of OP_*) */ (void)op; }
static void applyVolume(int vol)   { /* drive TV volume to vol (0..100) */ (void)vol; }
static void applyMuted(bool muted) { /* assert/clear mute */ (void)muted; }

// Control callback: invoked once per validated Alexa directive. Returning true
// means "handled" (Lambda reports success); returning false means unhandled
// (Lambda returns an Alexa error to the user). Dispatch is on cmd.code because
// both capabilities here are singletons (cmd.instance == kNoInstance throughout).
// Signature uses the fully-qualified automatica::CtlCommand on purpose — see the
// .ino prototype gotcha in the header block.
static bool onControl(const automatica::CtlCommand& cmd, void*) {
    switch (cmd.code) {
        case kCapPlayback:                       // 'y' — momentary: act on the op, persist nothing
            applyTransport(cmd.intVal);          //   cmd.intVal = OP_* transport op number
            return true;
        case kCapSpeaker:                        // 'u' — volume + mute
            applyVolume(cmd.intVal);             //   cmd.intVal  = target volume 0..100
            applyMuted(cmd.boolVal);             //   cmd.boolVal = muted flag
            return true;
    }
    return false;   // unknown/unhandled code -> Lambda returns an Alexa error
}

void setup() {
    // addEndpoint(id, friendlyName, description, displayCategories) -> endpoint idx.
    // The returned idx is the stable Alexa device index; pass it to every addXxx()
    // builder below so the capabilities attach to THIS endpoint.
    int ep = home.addEndpoint("tv", "TV", "automatica media player", {"TV"});

    home.addPlayback(ep);  // 'y' PlaybackController — singleton, momentary transport ops
    home.addSpeaker(ep);   // 'u' Speaker — singleton, volume 0..100 + muted

    // Seed the speaker's initial reported state (volume 15, not muted). Playback
    // is momentary and carries NO initial state, so we only seed the speaker.
    // For a singleton capability the instance arg is kNoInstance; CapState.i holds
    // the volume (0..100) and CapState.b holds muted.
    { CapState s; s.i = 15; s.b = false; home.setInitialState(ep, kCapSpeaker, kNoInstance, s); }

    home.onControl(onControl);  // register the dispatcher above
    home.begin();               // build the manifest + register the Particle variables/function (call once)
}

void loop() { home.loop(); }    // drive the core: flush coalesced publishes + emit the initial snapshot
