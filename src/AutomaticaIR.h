// AutomaticaIR.h — public, Device-OS-free API for the AutomaticaIR Particle class
// (IR_SPEC v0). A STANDALONE peer of the `Automatica` facade (NOT integrated into
// it): a cloud-connected IR blaster that records and replays infrared signals.
//
// It exposes Particle.functions to replay recorded IR signals (single / repeat-N /
// ordered sequence, all in ONE call) and to record new ones, plus
// Particle.variables that report capture state and paged captured signal data. It
// deliberately clones automatica's architecture:
//
//   * A pure, host-testable core (this file + AutomaticaIR.cpp) that is
//     std::string/int/std::vector only — NO Particle.h, NO Arduino String — so the
//     contract fixtures drive Catch2 tests on the desktop with PARTICLE undefined.
//   * A thin hardware seam (IRHardware.h) whose real impl (IRHardwarePhoton.h)
//     wraps the reused AnalysIR A.IR-Shield PWM/ISR code under #if defined(PARTICLE).
//   * A cloud seam (IRCloudPort.h) whose real impl (IRCloudParticle.h) maps onto
//     Particle.variable/function/publish.
//
// The IR signal CANONICAL FORM is the exact replay payload string
//   "freq:t0,t1,t2,…;"
// where freq is the carrier in kHz and t0,t1,… are alternating mark/space µs
// (t0 is a mark). This is stored verbatim by the backend so send-time transform is
// zero. See contract/IR_SPEC.md for the authoritative grammar.
//
// Namespacing: the class AutomaticaIR is declared in the GLOBAL namespace (mirrors
// how `Automatica` is declared). The supporting free functions, structs, enums, and
// contract constants live in the `automatica_ir` namespace to avoid global clashes
// while staying host-testable.
#ifndef AUTOMATICA_IR_H
#define AUTOMATICA_IR_H

#include <cstddef>
#include <string>
#include <vector>

#include "IRCloudPort.h"
#include "IRHardware.h"

namespace automatica_ir {

// ---- Contract constants (IR_SPEC.md §5, §6) --------------------------------

// Max chars per Particle.variable content page on Gen2 (irCapture0..N). Same cap
// automatica uses for its manifest pages. Units: characters. IR_SPEC.md §5.2.
static const size_t kPageSizeLimitChars = 864;

// Max chars accepted in a single irChunk/irSend function arg DATA slice. The
// deployed Device-OS Particle.function arg cap is 622 bytes; we cap chunk DATA well
// under that (the rest is the "transferId:seq:" prefix). Units: characters.
// IR_SPEC.md §3.
static const size_t kMaxChunkChars = 600;

// Upper bound on pulses (timings) in one signal. ~1024 covers even long AC-remote
// frames; beyond it we reject with IR_ERR_OVERFLOW to protect RAM on the MCU.
// Units: timing entries. IR_SPEC.md §6.
static const size_t kMaxPulses = 1024;

// Number of reserved paged capture variables (irCapture0 = header, then content).
// 8 = 1 header + up to 7 content pages * 864 chars ≈ 6048 chars of "freq:csv;" ≈
// ~1500 timings — above kMaxPulses, so a capture always fits. IR_SPEC.md §5.2.
static const int kCapturePageCount = 8;

// irState / capture state-machine values (IR_SPEC.md §5.1 state machine).
// Range: exactly these four values; serialized in irState as the leading token.
enum CaptureState {
    CAP_IDLE     = 0,  // not recording, no pending capture       (irState "idle")
    CAP_ARMED    = 1,  // armed, waiting for an IR frame           (irState "armed")
    CAP_CAPTURED = 2,  // a frame was captured; pages populated    (irState "captured")
    CAP_ERROR    = 3   // hardware/arm failure                     (irState "error")
};

// IR error/return codes (IR_SPEC.md §6 error codes). Mirror automatica's CtlStatus:
// 0 = OK, positive = a result count (transferId / next-seq / pulse count),
// negatives = errors. Range: exactly the values below.
enum IRStatus {
    IR_OK              =  0,  // success (or irRecord success)
    IR_ERR_PARSE       = -1,  // malformed arg / bad number / bad grammar
    IR_ERR_NO_TRANSFER = -2,  // irChunk/irSend referenced an unknown transferId
    IR_ERR_SEQ         = -3,  // chunk seq out of order
    IR_ERR_COUNT       = -4,  // committed chunk count != opened totalChunks / missing chunks / seq>=total
    IR_ERR_OVERFLOW    = -5,  // signal exceeds kMaxPulses (or page budget) / chunk > kMaxChunkChars
    IR_ERR_BUSY        = -6,  // send and capture are mutually exclusive
    IR_ERR_HARDWARE    = -7   // hardware send/arm failed
};

// ---- Parsed send payload ----------------------------------------------------

// One IR signal: carrier kHz + alternating mark/space µs timings. IR_SPEC.md §1.
struct Signal {
    int                       freqKHz;  // carrier kHz (e.g. 38); 0 = no-carrier sentinel
    std::vector<unsigned int> timings;  // µs, mark/space alternating, timings[0] is a mark
    Signal() : freqKHz(0) {}
};

// SendRequest is the fully-parsed result of an irSend payload (single-shot OR a
// committed chunked transfer). It captures the THREE batch primitives in one shape
// (IR_SPEC.md §2.1 batch grammar):
//   * single   : signals.size()==1, repeat==1
//   * repeat-N : signals.size()==1, repeat==N (>1)
//   * sequence : signals.size()>1, replayed in order with gapMicros between them
struct SendRequest {
    int                 status;     // IR_OK or an IR_ERR_* (IRStatus)
    std::vector<Signal> signals;    // 1..N signals (ordered)
    int                 repeat;     // replay count for the (single) signal; range >=1
    unsigned int        gapMicros;  // inter-signal gap for a sequence; units µs; range >=0
    SendRequest() : status(IR_OK), repeat(1), gapMicros(0) {}

    // Total pulse count across all signals * repeat — the value irSend returns to
    // the cloud on success (positive int). IR_SPEC.md §2.1.
    int pulseCount() const;
};

// ---- Free functions: pulse codec + payload parser (host-testable) -----------

// Parse a CANONICAL single signal string "freq:t0,t1,…;" into out. Trailing ';'
// optional. Returns IR_OK or IR_ERR_PARSE / IR_ERR_OVERFLOW. Empty timing list is
// a parse error (a signal must have at least one mark). IR_SPEC.md §1.
int parseSignal(const std::string& s, Signal& out);

// Encode a Signal back to canonical "freq:t0,t1,…;" form (round-trips parseSignal).
// Always emits the trailing ';'. IR_SPEC.md §1.
std::string encodeSignal(const Signal& sig);

// Parse a full irSend payload into a SendRequest. Accepts (IR_SPEC.md §2, §2.1):
//   * a single-shot canonical signal:            "38:9000,4500,560,1690;"
//   * a single-shot with options:                "38:9000,4500,...;|repeat=3"
//   * an ordered sequence with a gap:            "38:..;38:..;38:..;|gap=40000"
//     (or "...|seq|gap=40000" — the multiple ';'-terminated signals imply seq)
// The leading token before the first ':' is numeric (the carrier kHz), which is how
// irSend disambiguates a single-shot payload from a chunked commit
// "transferId:totalChunks" (the commit form has exactly one ':' and no ';'/','/'|').
SendRequest parseSendPayload(const std::string& payload);

// ---- Chunk accumulator -------------------------------------------------------

// Accumulates an out-of-band chunked transfer opened by irBegin and fed by irChunk,
// then assembled by irSend. Strictly in-order; bounded. IR_SPEC.md §3.
struct ChunkTransfer {
    int         id;          // rotating transferId (>=1); 0 = unused/no-transfer sentinel
    int         totalChunks; // declared in irBegin; range >=1
    int         nextSeq;     // next expected seq (0-based)
    std::string buf;         // concatenated chunk data so far
    ChunkTransfer() : id(0), totalChunks(0), nextSeq(0) {}
};

// ---- Capture page layout -----------------------------------------------------

// CapturePages holds the encoded capture split for the irCapture0..N variables.
// header (irCapture0) is "freq:totalPages:pulseCount"; content[i] are slices of the
// canonical "freq:csv;" signal string, each <= kPageSizeLimitChars. IR_SPEC.md §5.2.
struct CapturePages {
    std::string              header;   // irCapture0 = "freq:totalPages:pulseCount"
    std::vector<std::string> content;  // irCapture1..N, each <= kPageSizeLimitChars
};

// Split a captured Signal into paged variables (IR_SPEC.md §5.2 capture pages).
CapturePages buildCapturePages(const Signal& sig);

}  // namespace automatica_ir

// ---- The AutomaticaIR facade (GLOBAL namespace, peer of Automatica) ---------

// AutomaticaIR is the standalone IR-blaster class. Construct it with a cloud port +
// hardware seam, call begin() once from setup(), drive loop() each iteration. It
// registers the five cloud surfaces of IR_SPEC.md §0:
//   functions: irBegin, irChunk, irSend, irRecord
//   variables: irState, irCapture0..irCapture7
class AutomaticaIR {
public:
    // Construct against the cloud port and hardware seam. Neither is owned; both
    // must outlive this object. IR_SPEC.md §0.
    AutomaticaIR(automatica_ir::IRCloudPort& cloud, automatica_ir::IRHardware& hw);

    // Build state, register cloud variables/functions, init hardware. Call once
    // from setup() after construction. IR_SPEC.md §0.
    void begin();

    // Drive from loop(): poll the hardware for a completed capture and, when one
    // settles, encode it into the capture pages + advance state/seq. IR_SPEC.md §4.
    void loop();

    // ---- Particle.function handlers (also the host-test entry points) --------
    // Each returns an IRStatus-or-count int that flows straight back to the cloud.

    // irBegin("freqHint:totalChunks") — open a chunked transfer; returns a fresh
    // transferId (>=1) or IR_ERR_PARSE. freqHint is informational (the real freq is
    // embedded in the reassembled payload). Only ONE transfer is open at a time;
    // opening a new one discards any prior incomplete transfer. IR_SPEC.md §3.1.
    int handleBegin(const std::string& arg);

    // irChunk("transferId:seq:data") — append one in-order chunk; returns the next
    // expected seq, or IR_ERR_NO_TRANSFER / IR_ERR_SEQ / IR_ERR_OVERFLOW /
    // IR_ERR_COUNT / IR_ERR_PARSE. IR_SPEC.md §3.2.
    int handleChunk(const std::string& arg);

    // irSend(arg) — dual purpose (IR_SPEC.md §2, §3.3):
    //   * chunked commit "transferId:totalChunks": assemble, parse, send; or
    //   * single-shot canonical payload "freq:csv;[|opts]": parse, send.
    // Returns the positive pulse count on success or an IR_ERR_*.
    int handleSend(const std::string& arg);

    // irRecord("arm"|"cancel") — arm or cancel a capture. Returns IR_OK or
    // IR_ERR_BUSY (a send is mutually exclusive with capture) / IR_ERR_HARDWARE /
    // IR_ERR_PARSE. IR_SPEC.md §4.
    int handleRecord(const std::string& arg);

    // ---- accessors (tests + adapters) ----------------------------------------

    // Current capture state-machine value. IR_SPEC.md §5.1.
    automatica_ir::CaptureState captureState() const { return capState_; }
    // Monotonic capture sequence (bumps on each completed capture). IR_SPEC.md §5.1.
    int captureSeq() const { return capSeq_; }
    // Live backing string for the irState variable. IR_SPEC.md §5.1.
    const std::string& stateVar() const { return stateVar_; }
    // Live backing string for irCapture<i> (i in 0..kCapturePageCount-1). §5.2.
    const std::string& capturePage(int i) const;
    // Currently-open transferId, or 0 if none. IR_SPEC.md §3.
    int openTransferId() const { return xfer_.id; }

    // Execute a parsed SendRequest against the hardware (shared by single-shot and
    // chunked-commit paths). Returns pulse count or IR_ERR_*. Public for tests.
    // IR_SPEC.md §2.1.
    int dispatchSend(const automatica_ir::SendRequest& req);

private:
    void rebuildStateVar();              // recompute "<state>|<seq>|<pages>|<pulses>|<freq>"
    int  allocTransferId();              // rotating, never 0
    static std::string capPageName(int i);

    automatica_ir::IRCloudPort& cloud_;
    automatica_ir::IRHardware&  hw_;

    // Chunked-transfer state (single in-flight transfer).
    automatica_ir::ChunkTransfer xfer_;
    int           transferSeqCounter_;   // monotonic source of rotating ids

    // Capture state.
    automatica_ir::CaptureState capState_;
    int          capSeq_;                // bumps each completed capture
    int          capPageCount_;          // populated content pages of last capture
    int          capPulseCount_;         // pulse count of last capture
    int          capFreqKHz_;            // carrier kHz of last capture

    // Cloud-bound variable backing strings (addresses must stay stable after
    // begin(); the IRCloudParticle adapter mirrors them via refresh()).
    std::string  stateVar_;                                            // irState
    std::string  capturePages_[automatica_ir::kCapturePageCount];      // irCapture0..N-1

    bool begun_;
};

#endif  // AUTOMATICA_IR_H
