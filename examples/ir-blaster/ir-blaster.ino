// =============================================================================
// ir-blaster.ino — automatica example: IR universal remote blaster.
//
// DEVICE ARCHETYPE:
//   A Particle Photon Gen2 fitted with an AnalysIR A.IR Shield that records
//   and replays infrared signals on command from the Automatica cloud.
//
// CAPABILITIES SHOWN:
//   This sketch wires up the AutomaticaIR facade (cloud + hardware seam) and
//   calls begin() / loop(). That registers FOUR cloud functions and NINE cloud
//   variables automatically — no manual Particle.function / Particle.variable
//   calls are needed in the sketch:
//
//     Functions registered by begin():
//       irBegin   — open a chunked IR-signal transfer ("freqHint:totalChunks" -> transferId)
//       irChunk   — append one in-order chunk ("transferId:seq:data" -> next seq)
//       irSend    — replay a signal (single / repeat-N / ordered sequence), or
//                   commit a chunked transfer ("transferId:totalChunks" -> pulseCount)
//       irRecord  — arm or cancel IR capture ("arm" | "cancel" -> IR_OK)
//
//     Variables registered by begin():
//       irState      — "<state>|<seq>|<pageCount>|<pulseCount>|<freqKHz>"
//                      state is one of: idle / armed / captured / error
//       irCapture0   — capture HEADER page "freq:totalPages:pulseCount"
//       irCapture1..irCapture7 — capture CONTENT pages, each up to 864 chars of
//                                "freq:t0,t1,…;" (canonical form, IR_SPEC.md §1)
//
// ALEXA UTTERANCES THIS ENABLES (example friendly names — configured in console):
//   "Alexa, turn off the TV"                   (irSend sends the TV-off IR code)
//   "Alexa, mute the soundbar"                 (irSend sends the mute IR code)
//   "Alexa, set the AC to 72 degrees"          (irSend sends the AC set-temp code)
//   — any item whose control has a recorded IR code assigned to this blaster —
//
// HARDWARE:
//   Particle Photon Gen2 + AnalysIR A.IR Shield. No other components needed.
//   Pin map is fixed by the shield (do not change):
//     D2  — demodulated IR receiver (CHANGE ISR, TSOP-style)
//     D3  — modulated IR receiver, carrier frequency detection (FALLING ISR)
//     D7  — on-board status LED (blinks during send)
//     TX  — IR LED carrier output (TIM1 CH2 PWM)
//
// GOTCHAS:
//   * .ino preprocessor prototype rule: the Arduino/Particle toolchain auto-
//     generates function prototypes above the first #include line. If any
//     function in this file takes or returns a non-primitive type from a
//     library namespace (e.g. automatica_ir::CaptureState) the generated
//     prototype will be malformed. This sketch avoids that problem by keeping
//     setup() and loop() as the only non-static functions, both of which are
//     void(void) — no generated prototypes are emitted for them.
//   * IRCloudParticle and IRHardwarePhoton must be included AFTER AutomaticaIR.h
//     (which brings in IRCloudPort.h and IRHardware.h, their base classes).
//   * SYSTEM_MODE(AUTOMATIC) is mandatory — this firmware stays cloud-connected.
//     Do NOT use SYSTEM_MODE(MANUAL); the original AnalysIR sketch ran a raw TCP
//     server in MANUAL mode. This sketch does not.
//   * send and capture are mutually exclusive. Calling irSend while irRecord is
//     armed returns IR_ERR_BUSY (-6). The hardware automatically detaches the
//     capture ISRs for the duration of every send.
//   * There is only ONE in-flight chunked transfer at a time. Opening a new
//     irBegin discards any incomplete prior transfer.
// =============================================================================

#include "AutomaticaIR.h"
#include "IRHardwarePhoton.h"
#include "IRCloudParticle.h"

// Stay connected to the Particle Cloud. This is the ONLY supported mode.
SYSTEM_MODE(AUTOMATIC);

// ---- Concrete seam objects --------------------------------------------------
// IRHardwarePhoton: drives the A.IR Shield PWM carrier + edge-capture ISRs.
// IRCloudParticle:  maps the core's cloud seam onto Particle.variable/.function.
// Both must outlive `blaster` — declare them before it.
automatica_ir::IRHardwarePhoton  hw;
automatica_ir::IRCloudParticle   cloud;

// ---- AutomaticaIR facade ---------------------------------------------------
// Takes refs to the cloud port and hardware seam; owns neither.
// Registers irBegin / irChunk / irSend / irRecord and
// irState / irCapture0..irCapture7 inside begin().
AutomaticaIR blaster(cloud, hw);

void setup() {
    // Registers the 4 Particle.functions + 9 Particle.variables and initialises
    // the A.IR Shield hardware (PWM carrier, ISR pin modes). Call exactly once.
    blaster.begin();
}

void loop() {
    // When armed, polls the hardware for a completed IR capture. When one
    // settles (125 ms inter-frame gap), encodes it into the irCapture pages,
    // bumps the capture sequence counter, and updates irState to "captured".
    // In all other states this is a fast no-op.
    blaster.loop();
}
