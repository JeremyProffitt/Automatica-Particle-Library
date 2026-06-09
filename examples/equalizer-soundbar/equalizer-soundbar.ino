// equalizer-soundbar.ino — automatica example
// =============================================================================
// DEVICE ARCHETYPE: a soundbar / AV receiver with a 3-band graphic equalizer.
// One endpoint, two SINGLETON capabilities (each addressed by code, never by
// instance — see Gotchas).
//
// ALEXA CAPABILITIES EXPOSED
//   'o' Alexa.PowerController (singleton)
//        - TurnOn / TurnOff. Carried as a bool in cmd.boolVal.
//   'q' Alexa.EqualizerController (singleton, sub-op wire — SPEC §1.6 / §2.1)
//        - Bands: BASS / MIDRANGE / TREBLE, each an int level in -6..+6
//          (kEqBandMin..kEqBandMax). The Lambda resolves SetBands / AdjustBands /
//          ResetBands to ABSOLUTE levels before they reach the device, so the
//          handler always receives final values in cmd.bass / cmd.mid / cmd.treble
//          (no relative math on-device). Arrives as cmd.sub == kEqualizerSubBands.
//        - Modes: preset sound profiles MOVIE / MUSIC / NIGHT / SPORT / TV,
//          carried as a mode string in cmd.mode. Arrives as
//          cmd.sub == kEqualizerSubMode; the DEVICE loads its own named preset and
//          the facade tracks the resulting state.
//
// ALEXA UTTERANCES THIS ENABLES
//   "Alexa, set the bass to 4 on the soundbar"            -> q, kEqualizerSubBands
//   "Alexa, increase the treble on the soundbar"          -> q, kEqualizerSubBands (Lambda pre-resolves delta to absolute)
//   "Alexa, reset the equalizer on the soundbar"          -> q, kEqualizerSubBands (Lambda resolves to defaults)
//   "Alexa, set the soundbar equalizer to movie mode"     -> q, kEqualizerSubMode (cmd.mode == "MOVIE")
//   "Alexa, turn on the soundbar" / "...turn off..."      -> o, cmd.boolVal
//
// HARDWARE WIRING ASSUMPTION
//   D7 (onboard LED on most Particle devices) is driven as the power indicator.
//   applyBands()/applyMode() are stubs — wire them to your DSP / amplifier.
//
// GOTCHAS
//   - Both caps here are SINGLETONS: there is at most one PowerController and one
//     EqualizerController per endpoint. Singletons carry instance == kNoInstance on
//     the wire and are dispatched by cmd.code, NOT by instance index. The int that
//     addPower()/addEqualizer() return is always 0 and is intentionally ignored.
//   - The Automatica facade object is named `home`, NOT `automatica`, to avoid a
//     clash with `namespace automatica` (the `using namespace` above would make
//     `automatica home;` ambiguous).
//   - .ino preprocessor prototype rule: the Arduino/Particle preprocessor
//     auto-generates a forward prototype for onControl and inserts it at the TOP
//     of the file, ABOVE `using namespace automatica;`. So the prototype's
//     parameter type must be fully qualified as automatica::CtlCommand or the
//     generated prototype won't compile. (That is why the signature below is
//     fully-qualified.)
// =============================================================================
#include "automatica.h"
#include "AutomaticaCloud.h"

using namespace automatica;

Automatica home;                 // facade; never name it 'automatica' (namespace clash)
AutomaticaCloud cloud(home);     // ctor calls home.setCloudPort(this) — wires Particle.variable/function/publish

// --- hardware stubs: replace with your DSP / amplifier control ---
static void applyPower(bool on)                       { digitalWrite(D7, on ? HIGH : LOW); }
static void applyBands(int bass, int mid, int treble) { /* push absolute -6..+6 levels to the DSP */ (void)bass; (void)mid; (void)treble; }
static void applyMode(const std::string& mode)        { /* load the named preset MOVIE/MUSIC/NIGHT/SPORT/TV */ (void)mode; }

// Control callback. The facade has already decoded + validated the directive
// against this endpoint's declared capabilities, so cmd's typed fields for the
// matching code are populated (SPEC §1.6). Param type is fully-qualified as
// automatica::CtlCommand because the .ino-generated prototype lands above
// `using namespace automatica;` (see Gotchas).
//
// Reads: cmd.code (which capability), cmd.boolVal (power), cmd.sub (equalizer
//   op selector), cmd.bass/mid/treble (absolute band levels), cmd.mode (preset).
// Returns true when handled (the facade records the new state and the Lambda
//   reports success); returns false for anything we don't recognize, which the
//   Lambda turns into an Alexa error.
static bool onControl(const automatica::CtlCommand& cmd, void*) {
    switch (cmd.code) {
        case kCapPower:                     // 'o' singleton — dispatched by code, not instance
            applyPower(cmd.boolVal);        // cmd.boolVal: true = TurnOn, false = TurnOff
            return true;
        case kCapEqualizer:                 // 'q' singleton — cmd.sub picks the sub-op
            if (cmd.sub == kEqualizerSubBands) applyBands(cmd.bass, cmd.mid, cmd.treble); // absolute -6..+6 levels
            else                               applyMode(cmd.mode);                       // kEqualizerSubMode: preset string
            return true;
    }
    return false;   // unknown/unhandled code -> Lambda returns an Alexa error
}

void setup() {
    pinMode(D7, OUTPUT);

    // addEndpoint(id, friendlyName, description, displayCategories) -> Alexa idx.
    //   id "soundbar" must match ^[a-z0-9_-]{1,24}$ and stay stable across reboots
    //   (the endpoint's array index is the device identity). {"SPEAKER"} is the
    //   Alexa displayCategory shown in the app.
    int bar = home.addEndpoint("soundbar", "Soundbar", "automatica equalizer", {"SPEAKER"});

    home.addPower(bar);       // 'o' PowerController singleton (returns 0; ignored — addressed by code)
    home.addEqualizer(bar);   // 'q' EqualizerController singleton (returns 0; ignored — addressed by code)

    // Initial state pushed BEFORE begin() so the first cloud snapshot is correct.
    // For singletons pass the capability code with instance == kNoInstance.
    { CapState s; s.b = false; home.setInitialState(bar, kCapPower, kNoInstance, s); } // power off; CapState.b is the bool field for 'o'
    // Start flat (all bands 0) with the MOVIE preset active. For 'q', CapState
    // uses .bass/.mid/.treble (each kEqBandMin..kEqBandMax) and .mode (preset).
    { CapState s; s.bass = 0; s.mid = 0; s.treble = 0; s.mode = "MOVIE";
      home.setInitialState(bar, kCapEqualizer, kNoInstance, s); }

    home.onControl(onControl); // register the directive callback (no ctx needed here)
    home.begin();              // build manifest + register Particle.variable/function — call once
}

void loop() {
    home.loop();   // flush debounced state publishes (≤1/sec) + emit the initial snapshot
}
