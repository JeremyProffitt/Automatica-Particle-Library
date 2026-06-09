// step-speaker.ino — automatica example: an AV RECEIVER / TV driven by RELATIVE
// volume steps plus power, on ONE endpoint.
//
// DEVICE ARCHETYPE: a device you can only nudge (no absolute volume readout) — e.g.
// an AV receiver or TV controlled by firing IR up/down/mute commands. Pairs a
// stateful power switch with a stateless, momentary step-volume controller.
//
// ALEXA CAPABILITIES EXPOSED (two capabilities, both SINGLETON, one endpoint):
//   'o' Alexa.PowerController — STATEFUL bool ON/OFF (cmd.boolVal).
//   'g' Alexa.StepSpeaker     — MOMENTARY. Unlike 'u' Speaker it tracks NO absolute
//       volume; it carries a sub-op (cmd.sub) selecting the operation:
//         cmd.sub == kStepVolumeSub (0) : AdjustVolume — cmd.intVal is the SIGNED
//                                         step count (positive = up, negative = down).
//         cmd.sub == kStepMuteSub   (1) : SetMute      — cmd.boolVal is the mute state.
//       Because it is momentary it has NO stored state and needs NO setInitialState().
//
// ALEXA UTTERANCES this enables (friendlyName = "AV Receiver"):
//   "Alexa, turn on the receiver"               -> kCapPower, cmd.boolVal = true
//   "Alexa, turn up the volume on the receiver" -> kCapStepSpeaker, sub=0, intVal>0
//   "Alexa, volume down 3 on the receiver"      -> kCapStepSpeaker, sub=0, intVal=-3
//   "Alexa, mute the receiver"                  -> kCapStepSpeaker, sub=1, boolVal=true
//
// HARDWARE WIRING ASSUMPTION:
//   D7 drives a power relay (on-board LED on Photon/Argon/Boron, wiring-free demo).
//   irVolumeSteps()/irMute() stand in for an IR blaster or serial control line.
//
// GOTCHAS:
//   - StepSpeaker is MOMENTARY: do NOT call setInitialState() for kCapStepSpeaker
//     (it stores no state). Only the stateful PowerController is seeded.
//   - Read cmd.sub FIRST to know whether this is an AdjustVolume (use cmd.intVal,
//     signed) or a SetMute (use cmd.boolVal). The other field is not meaningful.
//   - Both capabilities here are SINGLETONS: routed by CODE (kCapPower /
//     kCapStepSpeaker) with kNoInstance, never by an instance index.
//   - Do not name the Automatica object 'automatica' (namespace clash). Use `home`.
#include "automatica.h"
#include "AutomaticaCloud.h"

using namespace automatica;

Automatica home;                 // never name it 'automatica' (namespace clash)
AutomaticaCloud cloud(home);     // ctor calls home.setCloudPort(this) — wires Particle.

// --- hardware stubs: replace with your IR blaster / serial control ---
static void applyPower(bool on)        { digitalWrite(D7, on ? HIGH : LOW); }
static void irVolumeSteps(int steps)   { /* fire |steps| vol-up or vol-down IR pulses */ (void)steps; }
static void irMute(bool mute)          { /* fire the mute IR command */ (void)mute; }

// Control callback. Fully-qualify CtlCommand (the .ino preprocessor emits the
// auto-prototype before the 'using namespace' line, so the unqualified name would
// not yet be in scope).
//   - cmd.code    : which capability (kCapPower 'o' or kCapStepSpeaker 'g').
//   - cmd.boolVal : PowerController ON/OFF; also StepSpeaker SetMute state.
//   - cmd.sub     : StepSpeaker sub-op (kStepVolumeSub=0 / kStepMuteSub=1).
//   - cmd.intVal  : StepSpeaker AdjustVolume signed step count.
//   - cmd.instance: kNoInstance for both (singletons — not read).
// Return true == handled (success to Alexa); false -> Lambda returns an Alexa error.
static bool onControl(const automatica::CtlCommand& cmd, void*) {
    switch (cmd.code) {
        case kCapPower:       applyPower(cmd.boolVal); return true;   // ON/OFF
        case kCapStepSpeaker:
            // Branch on the sub-op: AdjustVolume vs SetMute carry different fields.
            if (cmd.sub == kStepVolumeSub) irVolumeSteps(cmd.intVal); // signed step count
            else                           irMute(cmd.boolVal);       // mute on/off
            return true;
    }
    return false;   // unknown/unhandled -> Lambda returns an Alexa error
}

void setup() {
    pinMode(D7, OUTPUT);

    // Register the endpoint. id is the stable device identity (^[a-z0-9_-]{1,24}$);
    // {"TV"} display category. Returns the 0-based endpoint index used below.
    int avr = home.addEndpoint("av_receiver", "AV Receiver", "automatica step speaker", {"TV"});

    // Two singleton capabilities on the SAME endpoint. Return values (0) unused.
    home.addPower(avr);
    home.addStepSpeaker(avr);   // momentary: no setInitialState (StepSpeaker stores no state)

    // Initial state (BEFORE begin()): power OFF only. StepSpeaker is momentary and is
    // intentionally NOT seeded. Singleton -> kCapPower + kNoInstance; CapState.b = bool.
    { CapState s; s.b = false; home.setInitialState(avr, kCapPower, kNoInstance, s); }

    home.onControl(onControl);   // register the control callback (no user ctx)
    home.begin();                // build manifest + register Particle var/function. Call once.
}

void loop() {
    home.loop();   // flush debounced state publishes (<=1/sec) + emit initial snapshot.
}
