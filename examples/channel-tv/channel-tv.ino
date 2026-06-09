// =============================================================================
// channel-tv.ino — automatica example
// Device archetype: TV / set-top box with channel control (one endpoint,
// display category "TV"). One clearly-scoped concept: tuning channels via a
// capability that carries SUB-OPS (ChangeChannel vs SkipChannels) on one code.
//
// ALEXA CAPABILITIES EXPOSED (both SINGLETON — one per endpoint, no instance):
//   • Alexa.PowerController   (code 'o', kCapPower) — TurnOn/TurnOff (bool).
//   • Alexa.ChannelController (code 'f', kCapChannel) — addresses channels by
//     NUMBER (automatica has no station lineup / call-sign catalog). The single
//     code 'f' carries two operations selected by cmd.sub (SPEC §1.6):
//       - ChangeChannel -> cmd.sub == kChannelSubChange (0): cmd.intVal is the
//         ABSOLUTE channel number to tune to.
//       - SkipChannels  -> cmd.sub == kChannelSubSkip   (1): cmd.intVal is a
//         SIGNED RELATIVE count (+1 = channel up, -1 = channel down).
//     State is the current channel number (int). The library tracks/applies it
//     automatically AFTER the callback returns, so "what channel is the TV on?"
//     reports correctly.
//
// ALEXA UTTERANCES THIS ENABLES:
//   "Alexa, turn on the TV"
//   "Alexa, change the TV to channel 704"   (ChangeChannel, absolute -> cmd.intVal=704)
//   "Alexa, channel up on the TV"           (SkipChannels, relative  -> cmd.intVal=+1)
//   "Alexa, channel down on the TV"         (SkipChannels, relative  -> cmd.intVal=-1)
//
// HARDWARE WIRING ASSUMPTION:
//   D7 drives TV power (HIGH = on; on-board LED on most Particle devices).
//   tuneToChannel is a stub — wire it to your IR blaster or HDMI-CEC tuner.
//
// GOTCHAS:
//   • Both caps are SINGLETONS: addressed by cmd.code; cmd.instance is kNoInstance
//     and is not used. The two channel OPERATIONS are distinguished by cmd.sub,
//     NOT by instance.
//   • onControl fires BEFORE the facade applies the command to its tracked state
//     model, so for SkipChannels we must resolve the target ourselves by adding
//     the signed delta to our own gChannel mirror (we cannot read the post-apply
//     state from inside the callback).
//   • .ino preprocessor prototype rule: keep onControl's parameter fully qualified
//     as automatica::CtlCommand so the auto-generated prototype (emitted above the
//     `using namespace automatica;` line) matches the definition.
// =============================================================================
#include "automatica.h"
#include "AutomaticaCloud.h"

using namespace automatica;

// Core facade + Particle cloud adapter. Never name the facade 'automatica'
// (namespace clash). The adapter ctor calls home.setCloudPort(this).
Automatica home;                 // never name it 'automatica' (namespace clash)
AutomaticaCloud cloud(home);

// Our own mirror of the current channel number. We need it because SkipChannels
// is RELATIVE and the callback runs before the facade applies state — so we
// compute the new absolute channel here. Keep it in sync with setInitialState below.
int gChannel = 2;  // the current channel number (mirrors the facade's tracked state)

// --- hardware stubs: replace with your IR / HDMI-CEC tuner control ---
static void applyPower(bool on)      { digitalWrite(D7, on ? HIGH : LOW); } // HIGH = TV on
static void tuneToChannel(int number){ /* drive the tuner to `number` via IR/CEC */ (void)number; } // absolute channel

// Control callback. Reads:
//   cmd.code    — capability code; first-level dispatch.
//   cmd.boolVal — 'o' PowerController payload (true=on).
//   cmd.sub     — 'f' ChannelController sub-op: kChannelSubChange (absolute) or
//                 kChannelSubSkip (relative).
//   cmd.intVal  — 'f' payload: absolute channel number (Change) or signed step (Skip).
//   cmd.instance— kNoInstance (singletons); not read.
// Return true = handled (facade applies + reports the channel/power state).
// Fully-qualify CtlCommand (the .ino preprocessor emits the prototype before 'using namespace').
// NB: onControl fires BEFORE the facade applies the command to its state model, so we
// resolve the target channel ourselves (absolute for ChangeChannel, relative for Skip).
static bool onControl(const automatica::CtlCommand& cmd, void*) {
    switch (cmd.code) {
        case kCapPower:
            applyPower(cmd.boolVal);
            return true;
        case kCapChannel:
            if (cmd.sub == kChannelSubChange) gChannel = cmd.intVal;       // absolute number
            else { gChannel += cmd.intVal; if (gChannel < 0) gChannel = 0; } // signed skip count (clamp ≥ 0)
            tuneToChannel(gChannel);
            return true;
    }
    return false;   // unknown/unhandled -> Lambda returns an Alexa error
}

void setup() {
    pinMode(D7, OUTPUT);

    // Register the endpoint (idx == Alexa idx; declaration order is the device identity).
    int tv = home.addEndpoint("living_tv", "Living Room TV", "automatica TV tuner", {"TV"});

    // Declare the two SINGLETON capabilities. Singleton builders return 0 (unused) —
    // both are addressed by cmd.code, not by an instance index.
    home.addPower(tv);     // 'o' PowerController: TurnOn/TurnOff
    home.addChannel(tv);   // 'f' ChannelController: ChangeChannel/SkipChannels via cmd.sub

    // Initial state, applied before begin() so the first manifest + snapshot match.
    // Singletons -> instance kNoInstance.
    //   CapState.b -> Power bool (start off).
    //   CapState.i -> Channel number (start on channel 2). Keep gChannel above in sync.
    { CapState s; s.b = false; home.setInitialState(tv, kCapPower, kNoInstance, s); }
    { CapState s; s.i = 2;     home.setInitialState(tv, kCapChannel, kNoInstance, s); } // start on channel 2

    home.onControl(onControl);  // register dispatch callback
    home.begin();               // build manifest + register cloud var/function; call once
}

void loop() {
    home.loop();
}
