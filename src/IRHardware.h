// IRHardware.h — Device-OS-free seam over the IR carrier/capture hardware.
//
// The AutomaticaIR core drives IR send + capture through this abstract interface
// only; it never touches the STM32 timers, GPIO, or interrupts directly. On a
// Photon the real implementation is IRHardwarePhoton (guarded by
// #if defined(PARTICLE)), which wraps the reused AnalysIR A.IR-Shield PWM carrier
// + edge-capture ISR code. Host Catch2 tests inject a FakeHardware double that
// records sends and replays scripted captures, so the whole protocol/codec/state
// core is verified on a desktop with PARTICLE undefined.
//
// All types are std::-typed (no Particle.h / Arduino String) so the seam compiles
// on the host. Timings are alternating mark/space durations in MICROSECONDS; the
// first element is always a MARK (carrier on). freqKHz is the carrier frequency in
// kHz (e.g. 38). A freqKHz of 0 on capture is the "no carrier detected" sentinel.
#ifndef AUTOMATICA_IR_HARDWARE_H
#define AUTOMATICA_IR_HARDWARE_H

#include <cstdint>
#include <string>
#include <vector>

namespace automatica_ir {

// CaptureResult is one decoded capture handed up by pollCapture(): the detected
// carrier frequency (kHz, 0 = no carrier) and the alternating mark/space timings
// in microseconds (timings[0] is a mark).
struct CaptureResult {
    int                       freqKHz;
    std::vector<unsigned int> timings;  // µs, mark/space alternating, starts on mark
    CaptureResult() : freqKHz(0) {}
};

// IRHardware is the abstract hardware seam. Implementations must be non-blocking
// where noted; the core calls pollCapture() from loop().
class IRHardware {
public:
    virtual ~IRHardware() {}

    // One-time setup of timers/pins/ISRs. Called once from AutomaticaIR::begin().
    virtual void init() = 0;

    // Replay one IR signal `repeat` times back-to-back. freqKHz is the carrier
    // frequency; timings is alternating mark/space µs (timings[0] = mark). With
    // repeat <= 1 the signal is sent once; repeat == N sends it N times with no
    // extra inter-repeat gap beyond the signal's own trailing space.
    //
    // The implementation MUST detachInterrupt() around the carrier burst so the RX
    // capture ISR does not record our own transmission, then re-attach.
    virtual void sendRaw(int freqKHz,
                         const std::vector<unsigned int>& timings,
                         int repeat) = 0;

    // Replay an ordered SEQUENCE of signals (e.g. channel digits) with a fixed
    // inter-signal GAP in microseconds between consecutive signals. Each entry is
    // (freqKHz, timings). Used for the "sequence" batch primitive so the Lambda
    // makes a single Particle call per directive. Like sendRaw, this MUST guard the
    // bursts with detachInterrupt()/attachInterrupt().
    virtual void sendSequence(const std::vector<CaptureResult>& signals,
                              unsigned int gapMicros) = 0;

    // Arm the receiver to capture the next IR frame. Non-blocking: enables the RX
    // edge-capture ISR and resets the capture buffer. Returns false if the hardware
    // could not be armed (already busy, etc.).
    virtual bool armCapture() = 0;

    // Cancel an in-progress capture (disable the RX ISR, drop the buffer).
    virtual void cancelCapture() = 0;

    // Non-blocking poll for a completed capture. Returns true and fills `out` when
    // a frame has settled (end-of-frame timeout elapsed since the last edge), with
    // out.freqKHz == 0 meaning a frame was seen but no carrier could be measured.
    // Returns false while still armed/idle with nothing ready. Called from loop().
    virtual bool pollCapture(CaptureResult& out) = 0;
};

}  // namespace automatica_ir

#endif  // AUTOMATICA_IR_HARDWARE_H
