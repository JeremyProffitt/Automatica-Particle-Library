// AutomaticaIR.cpp — Device-OS-free core. std::string/int/std::vector only (no
// Particle.h / Arduino String) so host Catch2 tests link. The grammar + page
// layout MUST match contract/IR_SPEC.md and the fixtures under contract/fixtures/.
#include "AutomaticaIR.h"

#include <cstdlib>

namespace automatica_ir {

// ---------------------------------------------------------------------------
// Small parse helpers (no exceptions, no <sstream> — MCU-friendly).
// ---------------------------------------------------------------------------

// Parse a non-negative decimal integer from s[pos..end). Advances pos past the
// digits. Sets ok=false if no digit was consumed or the value overflows int.
static long parseUInt(const std::string& s, size_t& pos, bool& ok) {
    size_t start = pos;
    long v = 0;
    while (pos < s.size() && s[pos] >= '0' && s[pos] <= '9') {
        v = v * 10 + (s[pos] - '0');
        if (v > 2147483647L) { ok = false; return 0; }
        ++pos;
    }
    if (pos == start) { ok = false; return 0; }
    return v;
}

// Parse a signed decimal integer (used for the gap/repeat option values).
static long parseSInt(const std::string& s, size_t& pos, bool& ok) {
    bool neg = false;
    if (pos < s.size() && (s[pos] == '-' || s[pos] == '+')) { neg = (s[pos] == '-'); ++pos; }
    long v = parseUInt(s, pos, ok);
    return neg ? -v : v;
}

// Trim surrounding ASCII whitespace.
static std::string trim(const std::string& s) {
    size_t a = 0, b = s.size();
    while (a < b && (s[a] == ' ' || s[a] == '\t' || s[a] == '\r' || s[a] == '\n')) ++a;
    while (b > a && (s[b - 1] == ' ' || s[b - 1] == '\t' || s[b - 1] == '\r' || s[b - 1] == '\n')) --b;
    return s.substr(a, b - a);
}

// Append a non-negative int as decimal to o.
static void appendInt(std::string& o, long v) {
    if (v < 0) { o.push_back('-'); v = -v; }
    char buf[16];
    int n = 0;
    if (v == 0) buf[n++] = '0';
    while (v > 0) { buf[n++] = (char)('0' + (v % 10)); v /= 10; }
    while (n > 0) o.push_back(buf[--n]);
}

// ---------------------------------------------------------------------------
// SendRequest helpers
// ---------------------------------------------------------------------------

int SendRequest::pulseCount() const {
    long total = 0;
    for (size_t i = 0; i < signals.size(); ++i) total += (long)signals[i].timings.size();
    // For a single signal the repeat multiplies; sequences are sent once each.
    if (signals.size() == 1) total *= (repeat < 1 ? 1 : repeat);
    if (total > 2147483647L) total = 2147483647L;
    return (int)total;
}

// ---------------------------------------------------------------------------
// Pulse codec (canonical "freq:t0,t1,…;")
// ---------------------------------------------------------------------------

int parseSignal(const std::string& in, Signal& out) {
    std::string s = trim(in);
    if (s.empty()) return IR_ERR_PARSE;
    // Drop a single trailing ';'.
    if (s[s.size() - 1] == ';') s.erase(s.size() - 1);

    size_t pos = 0;
    bool ok = true;
    long freq = parseUInt(s, pos, ok);
    if (!ok) return IR_ERR_PARSE;
    if (pos >= s.size() || s[pos] != ':') return IR_ERR_PARSE;
    ++pos;  // skip ':'

    out.freqKHz = (int)freq;
    out.timings.clear();

    // At least one timing required.
    while (pos < s.size()) {
        ok = true;
        long t = parseUInt(s, pos, ok);
        if (!ok) return IR_ERR_PARSE;
        if (t < 0) return IR_ERR_PARSE;
        out.timings.push_back((unsigned int)t);
        if (out.timings.size() > kMaxPulses) return IR_ERR_OVERFLOW;
        if (pos >= s.size()) break;
        if (s[pos] == ',') {
            ++pos;
            // A ',' MUST be followed by another timing — reject a trailing comma.
            if (pos >= s.size()) return IR_ERR_PARSE;
            continue;
        }
        return IR_ERR_PARSE;  // unexpected char
    }
    if (out.timings.empty()) return IR_ERR_PARSE;
    return IR_OK;
}

std::string encodeSignal(const Signal& sig) {
    std::string o;
    appendInt(o, sig.freqKHz);
    o.push_back(':');
    for (size_t i = 0; i < sig.timings.size(); ++i) {
        if (i) o.push_back(',');
        appendInt(o, (long)sig.timings[i]);
    }
    o.push_back(';');
    return o;
}

// ---------------------------------------------------------------------------
// irSend payload parser (single / repeat-N / sequence)
// ---------------------------------------------------------------------------

SendRequest parseSendPayload(const std::string& payload) {
    SendRequest req;
    std::string s = trim(payload);
    if (s.empty()) { req.status = IR_ERR_PARSE; return req; }

    // Split off the options tail after a '|': "<signals>|opt=val|opt=val|seq".
    std::string sigPart = s;
    std::vector<std::string> opts;
    {
        size_t bar = s.find('|');
        if (bar != std::string::npos) {
            sigPart = s.substr(0, bar);
            size_t p = bar + 1;
            while (p <= s.size()) {
                size_t next = s.find('|', p);
                std::string tok = (next == std::string::npos) ? s.substr(p) : s.substr(p, next - p);
                tok = trim(tok);
                if (!tok.empty()) opts.push_back(tok);
                if (next == std::string::npos) break;
                p = next + 1;
            }
        }
    }

    // Parse one or more ';'-terminated signals from sigPart.
    sigPart = trim(sigPart);
    {
        size_t pos = 0;
        while (pos < sigPart.size()) {
            size_t semi = sigPart.find(';', pos);
            std::string one;
            if (semi == std::string::npos) {
                one = sigPart.substr(pos);
                pos = sigPart.size();
            } else {
                one = sigPart.substr(pos, semi - pos);
                pos = semi + 1;
            }
            one = trim(one);
            if (one.empty()) continue;  // tolerate trailing ';'
            Signal sig;
            int st = parseSignal(one, sig);
            if (st != IR_OK) { req.status = st; return req; }
            req.signals.push_back(sig);
        }
    }
    if (req.signals.empty()) { req.status = IR_ERR_PARSE; return req; }

    // Apply options.
    for (size_t i = 0; i < opts.size(); ++i) {
        const std::string& o = opts[i];
        if (o == "seq") continue;  // explicit sequence marker (also implied by >1 signal)
        size_t eq = o.find('=');
        if (eq == std::string::npos) { req.status = IR_ERR_PARSE; return req; }
        std::string key = o.substr(0, eq);
        std::string valStr = o.substr(eq + 1);
        size_t vp = 0; bool ok = true;
        if (key == "repeat") {
            long v = parseUInt(valStr, vp, ok);
            if (!ok || vp != valStr.size() || v < 1) { req.status = IR_ERR_PARSE; return req; }
            req.repeat = (int)v;
        } else if (key == "gap") {
            long v = parseSInt(valStr, vp, ok);
            if (!ok || vp != valStr.size() || v < 0) { req.status = IR_ERR_PARSE; return req; }
            req.gapMicros = (unsigned int)v;
        } else {
            req.status = IR_ERR_PARSE; return req;  // unknown option
        }
    }

    // Total pulse budget guard across a sequence.
    long total = 0;
    for (size_t i = 0; i < req.signals.size(); ++i) total += (long)req.signals[i].timings.size();
    if (req.signals.size() == 1) total *= (req.repeat < 1 ? 1 : req.repeat);
    if (total > (long)kMaxPulses * 8) { req.status = IR_ERR_OVERFLOW; return req; }

    req.status = IR_OK;
    return req;
}

// ---------------------------------------------------------------------------
// Capture page layout
// ---------------------------------------------------------------------------

CapturePages buildCapturePages(const Signal& sig) {
    CapturePages pages;
    std::string body = encodeSignal(sig);  // canonical "freq:csv;"
    for (size_t i = 0; i < body.size(); i += kPageSizeLimitChars) {
        size_t n = body.size() - i;
        if (n > kPageSizeLimitChars) n = kPageSizeLimitChars;
        pages.content.push_back(body.substr(i, n));
    }
    // Header: "freq:totalPages:pulseCount".
    std::string h;
    appendInt(h, sig.freqKHz);
    h.push_back(':');
    appendInt(h, (long)pages.content.size());
    h.push_back(':');
    appendInt(h, (long)sig.timings.size());
    pages.header = h;
    return pages;
}

}  // namespace automatica_ir

// ---------------------------------------------------------------------------
// AutomaticaIR facade
// ---------------------------------------------------------------------------

using namespace automatica_ir;

static int beginTrampoline(const std::string& arg, void* ctx)  { return static_cast<AutomaticaIR*>(ctx)->handleBegin(arg); }
static int chunkTrampoline(const std::string& arg, void* ctx)  { return static_cast<AutomaticaIR*>(ctx)->handleChunk(arg); }
static int sendTrampoline(const std::string& arg, void* ctx)   { return static_cast<AutomaticaIR*>(ctx)->handleSend(arg); }
static int recordTrampoline(const std::string& arg, void* ctx) { return static_cast<AutomaticaIR*>(ctx)->handleRecord(arg); }

AutomaticaIR::AutomaticaIR(IRCloudPort& cloud, IRHardware& hw)
    : cloud_(cloud), hw_(hw),
      transferSeqCounter_(0),
      capState_(CAP_IDLE), capSeq_(0), capPageCount_(0), capPulseCount_(0), capFreqKHz_(0),
      begun_(false) {}

std::string AutomaticaIR::capPageName(int i) {
    std::string nm = "irCapture";
    appendInt(nm, (long)i);
    return nm;
}

const std::string& AutomaticaIR::capturePage(int i) const {
    static const std::string kEmpty;
    if (i < 0 || i >= kCapturePageCount) return kEmpty;
    return capturePages_[i];
}

void AutomaticaIR::rebuildStateVar() {
    // "<state>|<seq>|<pageCount>|<pulseCount>|<freqKHz>"
    std::string s;
    switch (capState_) {
        case CAP_IDLE:     s = "idle"; break;
        case CAP_ARMED:    s = "armed"; break;
        case CAP_CAPTURED: s = "captured"; break;
        case CAP_ERROR:    s = "error"; break;
        default:           s = "idle"; break;
    }
    s.push_back('|'); appendInt(s, (long)capSeq_);
    s.push_back('|'); appendInt(s, (long)capPageCount_);
    s.push_back('|'); appendInt(s, (long)capPulseCount_);
    s.push_back('|'); appendInt(s, (long)capFreqKHz_);
    stateVar_ = s;
    cloud_.refresh("irState");
}

int AutomaticaIR::allocTransferId() {
    // Rotating, never 0 (0 is the "no transfer" sentinel). Wrap at a large bound.
    ++transferSeqCounter_;
    if (transferSeqCounter_ <= 0) transferSeqCounter_ = 1;
    if (transferSeqCounter_ > 1000000) transferSeqCounter_ = 1;
    return transferSeqCounter_;
}

void AutomaticaIR::begin() {
    // Register capture variables (addresses stable for the device lifetime).
    rebuildStateVar();
    cloud_.registerVariable("irState", &stateVar_);
    for (int i = 0; i < kCapturePageCount; ++i) {
        cloud_.registerVariable(capPageName(i), &capturePages_[i]);
    }
    // Register the four IR functions.
    cloud_.registerFunction("irBegin",  &beginTrampoline,  this);
    cloud_.registerFunction("irChunk",  &chunkTrampoline,  this);
    cloud_.registerFunction("irSend",   &sendTrampoline,   this);
    cloud_.registerFunction("irRecord", &recordTrampoline, this);

    hw_.init();
    begun_ = true;
}

void AutomaticaIR::loop() {
    if (!begun_) return;
    if (capState_ != CAP_ARMED) return;

    CaptureResult res;
    if (!hw_.pollCapture(res)) return;

    // A frame settled. Encode into pages, bump seq, set state.
    Signal sig;
    sig.freqKHz = res.freqKHz;
    sig.timings = res.timings;

    // Clear all page slots first (a shorter capture must not leave stale pages).
    for (int i = 0; i < kCapturePageCount; ++i) {
        if (!capturePages_[i].empty()) { capturePages_[i].clear(); cloud_.refresh(capPageName(i)); }
    }

    CapturePages pages = buildCapturePages(sig);
    // Guard against overflowing the reserved content pages (irCapture1..N-1).
    if ((int)pages.content.size() > kCapturePageCount - 1) {
        capState_ = CAP_ERROR;
        capPageCount_ = 0;
        capPulseCount_ = 0;
        capFreqKHz_ = 0;
        rebuildStateVar();
        return;
    }

    capturePages_[0] = pages.header;
    cloud_.refresh(capPageName(0));
    for (size_t i = 0; i < pages.content.size(); ++i) {
        capturePages_[1 + i] = pages.content[i];
        cloud_.refresh(capPageName(1 + (int)i));
    }

    capPageCount_  = (int)pages.content.size();
    capPulseCount_ = (int)sig.timings.size();
    capFreqKHz_    = sig.freqKHz;
    capSeq_       += 1;
    capState_      = CAP_CAPTURED;
    rebuildStateVar();
}

int AutomaticaIR::handleBegin(const std::string& arg) {
    // "freqHint:totalChunks"
    std::string s = trim(arg);
    size_t pos = 0; bool ok = true;
    (void)parseUInt(s, pos, ok);  // freqHint (informational; embedded in payload)
    if (!ok || pos >= s.size() || s[pos] != ':') return IR_ERR_PARSE;
    ++pos;
    long total = parseUInt(s, pos, ok);
    if (!ok || pos != s.size() || total < 1) return IR_ERR_PARSE;

    // Open a fresh transfer (discards any prior incomplete one).
    xfer_ = ChunkTransfer();
    xfer_.id = allocTransferId();
    xfer_.totalChunks = (int)total;
    xfer_.nextSeq = 0;
    xfer_.buf.clear();
    return xfer_.id;
}

int AutomaticaIR::handleChunk(const std::string& arg) {
    // "transferId:seq:data" — data may itself contain ':'/',' /';', so split on the
    // FIRST TWO ':' only.
    size_t c1 = arg.find(':');
    if (c1 == std::string::npos) return IR_ERR_PARSE;
    size_t c2 = arg.find(':', c1 + 1);
    if (c2 == std::string::npos) return IR_ERR_PARSE;

    std::string idStr  = arg.substr(0, c1);
    std::string seqStr = arg.substr(c1 + 1, c2 - c1 - 1);
    std::string data   = arg.substr(c2 + 1);

    size_t p = 0; bool ok = true;
    long id = parseUInt(idStr, p, ok);
    if (!ok || p != idStr.size()) return IR_ERR_PARSE;
    p = 0; ok = true;
    long seq = parseUInt(seqStr, p, ok);
    if (!ok || p != seqStr.size()) return IR_ERR_PARSE;

    if (xfer_.id == 0 || (int)id != xfer_.id) return IR_ERR_NO_TRANSFER;
    if ((int)seq != xfer_.nextSeq) return IR_ERR_SEQ;
    if (data.size() > kMaxChunkChars) return IR_ERR_OVERFLOW;
    if (seq >= xfer_.totalChunks) return IR_ERR_COUNT;

    xfer_.buf += data;
    xfer_.nextSeq += 1;
    return xfer_.nextSeq;  // next expected seq
}

int AutomaticaIR::handleSend(const std::string& arg) {
    if (capState_ == CAP_ARMED) return IR_ERR_BUSY;  // capture in progress

    std::string s = trim(arg);
    if (s.empty()) return IR_ERR_PARSE;

    // Disambiguate chunked-commit "transferId:totalChunks" from a single-shot
    // canonical payload. The commit form has exactly one ':' and contains no ','
    // or ';' (a single signal always has timings, hence ',' or ';'/multiple, and
    // its leading number is the carrier kHz, not a transferId). We treat it as a
    // commit only if it parses as two ints AND the first matches the open transfer.
    bool hasComma = s.find(',') != std::string::npos;
    bool hasSemi  = s.find(';') != std::string::npos;
    bool hasBar   = s.find('|') != std::string::npos;
    size_t colon1 = s.find(':');
    size_t colon2 = (colon1 == std::string::npos) ? std::string::npos : s.find(':', colon1 + 1);

    bool looksLikeCommit = (!hasComma && !hasSemi && !hasBar &&
                            colon1 != std::string::npos && colon2 == std::string::npos);

    if (looksLikeCommit) {
        std::string idStr = s.substr(0, colon1);
        std::string nStr  = s.substr(colon1 + 1);
        size_t p = 0; bool ok = true;
        long id = parseUInt(idStr, p, ok);
        bool idOk = ok && p == idStr.size();
        p = 0; ok = true;
        long n = parseUInt(nStr, p, ok);
        bool nOk = ok && p == nStr.size();
        if (idOk && nOk) {
            if (xfer_.id == 0 || (int)id != xfer_.id) return IR_ERR_NO_TRANSFER;
            if ((int)n != xfer_.totalChunks) return IR_ERR_COUNT;
            if (xfer_.nextSeq != xfer_.totalChunks) return IR_ERR_COUNT;  // not all chunks received
            SendRequest req = parseSendPayload(xfer_.buf);
            // Consume the transfer regardless of parse outcome.
            std::string committed = xfer_.buf;
            xfer_ = ChunkTransfer();
            if (req.status != IR_OK) return req.status;
            return dispatchSend(req);
        }
        // Fall through: not a commit (e.g. "38:0" — but that has no timings and is
        // a parse error anyway).
    }

    // Single-shot canonical payload.
    SendRequest req = parseSendPayload(s);
    if (req.status != IR_OK) return req.status;
    return dispatchSend(req);
}

int AutomaticaIR::dispatchSend(const SendRequest& req) {
    if (req.signals.empty()) return IR_ERR_PARSE;

    if (req.signals.size() == 1) {
        int repeat = req.repeat < 1 ? 1 : req.repeat;
        hw_.sendRaw(req.signals[0].freqKHz, req.signals[0].timings, repeat);
    } else {
        // Sequence: convert to CaptureResult shape the hardware seam consumes.
        std::vector<CaptureResult> seq;
        seq.reserve(req.signals.size());
        for (size_t i = 0; i < req.signals.size(); ++i) {
            CaptureResult cr;
            cr.freqKHz = req.signals[i].freqKHz;
            cr.timings = req.signals[i].timings;
            seq.push_back(cr);
        }
        hw_.sendSequence(seq, req.gapMicros);
    }
    return req.pulseCount();
}

int AutomaticaIR::handleRecord(const std::string& arg) {
    std::string s = trim(arg);
    if (s == "arm") {
        if (capState_ == CAP_ARMED) return IR_OK;  // idempotent
        if (!hw_.armCapture()) {
            capState_ = CAP_ERROR;
            rebuildStateVar();
            return IR_ERR_HARDWARE;
        }
        capState_ = CAP_ARMED;
        rebuildStateVar();
        return IR_OK;
    }
    if (s == "cancel") {
        hw_.cancelCapture();
        capState_ = CAP_IDLE;
        rebuildStateVar();
        return IR_OK;
    }
    return IR_ERR_PARSE;
}
