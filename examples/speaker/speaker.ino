// speaker.ino — automatica example: a POWERED SPEAKER with ABSOLUTE volume + mute.
//
// DEVICE ARCHETYPE: a speaker/amp whose volume is a stateful 0..100 level you can
// set directly (and read back), plus a mute toggle. One endpoint, one capability.
//
// ALEXA CAPABILITIES EXPOSED:
//   'u' Alexa.Speaker — SINGLETON. A STATEFUL capability that carries BOTH:
//       - volume : int 0..100 (no engineering unit; a percentage-style level),
//                  delivered to the callback in cmd.intVal and stored in CapState.i.
//       - muted  : bool, delivered in cmd.boolVal and stored in CapState.b.
//       Alexa.Speaker is the ABSOLUTE-volume controller (SetVolume/SetMute),
//       contrasted with 'g' StepSpeaker (relative, momentary — see step-speaker.ino).
//
// ALEXA UTTERANCES this enables (friendlyName = "Speaker"):
//   "Alexa, set the volume to 30 on the speaker"   -> cmd.intVal = 30
//   "Alexa, mute the speaker"                       -> cmd.boolVal = true
//   "Alexa, unmute the speaker"                     -> cmd.boolVal = false
//
// HARDWARE WIRING ASSUMPTION: none fixed — wire applyVolume()/applyMuted() to your
// amp/DAC gain and mute lines.
//
// GOTCHAS:
//   - Alexa.Speaker is a SINGLETON: routed by CODE (kCapSpeaker) with kNoInstance,
//     not by an instance index. addSpeaker() returns 0, unused.
//   - It is STATEFUL: seed both fields via setInitialState() (CapState.i = volume,
//     CapState.b = muted). Contrast StepSpeaker which is stateless/momentary.
//   - One CtlCommand carries both views (intVal + boolVal); for SetVolume the bool
//     reflects current mute and for SetMute the int reflects current volume — apply
//     both each time so the device stays consistent with the reported state.
//   - Do not name the Automatica object 'automatica' (namespace clash). Use `home`.
#include "automatica.h"
#include "AutomaticaCloud.h"

using namespace automatica;

Automatica home;                 // never name it 'automatica' (namespace clash)
AutomaticaCloud cloud(home);     // ctor calls home.setCloudPort(this) — wires Particle.

// --- hardware stubs: replace with your amp/DAC volume + mute lines ---
static void applyVolume(int vol)  { /* drive amp gain 0..100 */ (void)vol; }
static void applyMuted(bool muted) { /* assert mute line */ (void)muted; }

// Control callback for Alexa.Speaker.
// Fully-qualify CtlCommand (the .ino preprocessor emits the auto-prototype before
// the 'using namespace' line, so the unqualified name would not yet be in scope).
//   - cmd.code    : kCapSpeaker ('u') for this endpoint.
//   - cmd.intVal  : requested absolute volume 0..100 (SetVolume).
//   - cmd.boolVal : requested mute state (SetMute).
//   - cmd.instance: kNoInstance (singleton — not read).
// Return true == handled (success to Alexa); false -> Lambda returns an Alexa error.
static bool onControl(const automatica::CtlCommand& cmd, void*) {
    switch (cmd.code) {
        case kCapSpeaker:
            // Alexa sends SetVolume (intVal) or SetMute (boolVal); apply both views
            // so the hardware always matches the full reported Speaker state.
            applyVolume(cmd.intVal);
            applyMuted(cmd.boolVal);
            return true;
    }
    return false;   // unknown/unhandled -> Lambda returns an Alexa error
}

void setup() {
    // Register the endpoint. id is the stable device identity (^[a-z0-9_-]{1,24}$);
    // {"SPEAKER"} display category makes Alexa render a speaker. Returns endpoint idx.
    int ep = home.addEndpoint("speaker", "Speaker", "automatica speaker", {"SPEAKER"});

    // Add the singleton Alexa.Speaker ('u'). Return value (0) unused for singletons.
    home.addSpeaker(ep);

    // Initial state (BEFORE begin()): volume 20, not muted. Singleton -> kCapSpeaker
    // + kNoInstance. CapState.i = volume (0..100), CapState.b = muted. Seeds the
    // first state snapshot reported to Alexa.
    { CapState s; s.i = 20; s.b = false; home.setInitialState(ep, kCapSpeaker, kNoInstance, s); }

    home.onControl(onControl);   // register the control callback (no user ctx)
    home.begin();                // build manifest + register Particle var/function. Call once.
}

void loop() { home.loop(); }   // flush debounced publishes (<=1/sec) + initial snapshot.
