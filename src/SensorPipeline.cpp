// SensorPipeline.cpp — Device-OS-free sensor smoothing, calibration, and report-gating
// engine implementation. See SensorPipeline.h for the full design note + SPEC §12.
//
// Device-OS-free: only <string>, <vector>, <cstdlib>, <cstring>, <cstdint>, <cmath>.
// No Arduino headers. Compiles clean on the host Catch2 build.

#include "SensorPipeline.h"

#include <cstdlib>   // atof, atoi
#include <cstring>   // strcmp, strlen
#include <cmath>     // fabsf, isnan (host), INFINITY

namespace automatica {

// ===========================================================================
// SensorPipeline public API
// ===========================================================================

SensorPipeline::SensorPipeline() {}

void SensorPipeline::setChannel(const std::string& name, const ChannelConfig& cfg) {
    ChannelState& st = findOrCreate(name);
    st.cfg          = cfg;
    // Reset all filter state so the new config takes effect from a clean slate.
    st.emaPrev       = 0.0f;
    st.emaInitialized = false;
    st.medBuf[0] = st.medBuf[1] = st.medBuf[2] = 0.0f;
    st.medCount  = 0;
    st.lastReported = 0.0f;
    st.everReported = false;
    st.hystActive   = false;
}

Result SensorPipeline::process(const std::string& name, float rawValue,
                                unsigned long /*nowMs*/) {
    ChannelState& st = findOrCreate(name);

    // Step 1 — Calibration (multi-point table or linear). SPEC §12.2.
    float calValue = applyCal(rawValue, st.cfg);

    // Step 2 — EMA smoothing. SPEC §12.3.
    float emaValue = applyEma(calValue, st);

    // Step 3 — Median filter. SPEC §12.4.
    float filteredValue = applyMedian(emaValue, st);

    // Step 4 — Hysteresis (updates st.hystActive). SPEC §12.6.
    bool hActive = applyHyst(filteredValue, st);

    // Step 5 — Deadband gating (decides shouldReport). SPEC §12.5.
    bool report = applyDeadband(filteredValue, st);

    Result r;
    r.shouldReport = report;
    r.value        = filteredValue;
    r.hystActive   = hActive;
    return r;
}

bool SensorPipeline::parseConfig(const std::string& rawJson) {
    // Locate the top-level "sensorPipeline" object. SPEC §12.8.
    bool found = false;
    std::string spObj = jsonFindObject(rawJson, "sensorPipeline", found);
    if (!found || spObj.empty()) return false;

    // Locate "channels" sub-object.
    std::string channelsObj = jsonFindObject(spObj, "channels", found);
    if (!found || channelsObj.empty()) return false;

    iterateChannelsObject(channelsObj);
    return true;
}

size_t SensorPipeline::channelCount() const {
    return channels_.size();
}

bool SensorPipeline::hasChannel(const std::string& name) const {
    for (size_t i = 0; i < channels_.size(); ++i) {
        if (channels_[i].name == name) return true;
    }
    return false;
}

// ===========================================================================
// Private — channel lookup
// ===========================================================================

SensorPipeline::ChannelState& SensorPipeline::findOrCreate(const std::string& name) {
    for (size_t i = 0; i < channels_.size(); ++i) {
        if (channels_[i].name == name) return channels_[i];
    }
    ChannelState st;
    st.name = name;
    channels_.push_back(st);
    return channels_.back();
}

// ===========================================================================
// Private — pipeline stages
// ===========================================================================

float SensorPipeline::applyCal(float raw, const ChannelConfig& cfg) {
    // Multi-point piecewise-linear calibration takes priority. SPEC §12.2.
    if (!cfg.calTable.empty()) {
        // Table must be sorted ascending by .raw. Clamp at ends.
        const std::vector<CalPoint>& tbl = cfg.calTable;
        if (raw <= tbl[0].raw) return tbl[0].calibrated;
        if (raw >= tbl[tbl.size() - 1].raw) return tbl[tbl.size() - 1].calibrated;
        // Find the bracketing interval and interpolate.
        for (size_t i = 0; i + 1 < tbl.size(); ++i) {
            if (raw >= tbl[i].raw && raw <= tbl[i + 1].raw) {
                float span = tbl[i + 1].raw - tbl[i].raw;
                if (span == 0.0f) return tbl[i].calibrated;
                float t = (raw - tbl[i].raw) / span;
                return tbl[i].calibrated + t * (tbl[i + 1].calibrated - tbl[i].calibrated);
            }
        }
        return raw; // unreachable — but safe
    }
    // Linear calibration: cal = scale * raw + offset. SPEC §12.2.
    return cfg.calScale * raw + cfg.calOffset;
}

float SensorPipeline::applyEma(float calValue, ChannelState& state) {
    const float alpha = state.cfg.emaAlpha;
    // alpha == 1.0 means pass-through (no smoothing). SPEC §12.3.
    if (alpha >= 1.0f) {
        // Still initialize emaPrev so future alpha changes work correctly.
        if (!state.emaInitialized) { state.emaPrev = calValue; state.emaInitialized = true; }
        return calValue;
    }
    if (!state.emaInitialized) {
        state.emaPrev       = calValue;
        state.emaInitialized = true;
        return calValue;
    }
    float out    = alpha * calValue + (1.0f - alpha) * state.emaPrev;
    state.emaPrev = out;
    return out;
}

// medianOf3 — branchless 3-sample median. SPEC §12.4.
static float medianOf3(float a, float b, float c) {
    // Sort a, b, c and return the middle value.
    float lo = a, mid = b, hi = c;
    if (lo > mid) { float t = lo; lo = mid; mid = t; }
    if (mid > hi) { float t = mid; mid = hi; hi = t; }
    if (lo > mid) { float t = lo; lo = mid; mid = t; }
    (void)lo; (void)hi;
    return mid;
}

float SensorPipeline::applyMedian(float emaValue, ChannelState& state) {
    // medianWindow == 3 → maintain a 3-sample ring buffer. SPEC §12.4.
    if (state.cfg.medianWindow != 3) return emaValue;

    // Fill the ring buffer left-to-right until we have 3 samples.
    int cnt = state.medCount;
    if (cnt < 3) {
        state.medBuf[cnt] = emaValue;
        state.medCount    = cnt + 1;
        if (cnt < 2) return emaValue; // not enough samples for median yet; pass through
        // Now we have 3 samples.
        return medianOf3(state.medBuf[0], state.medBuf[1], state.medBuf[2]);
    }
    // Shift left and append.
    state.medBuf[0] = state.medBuf[1];
    state.medBuf[1] = state.medBuf[2];
    state.medBuf[2] = emaValue;
    return medianOf3(state.medBuf[0], state.medBuf[1], state.medBuf[2]);
}

bool SensorPipeline::applyDeadband(float value, ChannelState& state) {
    // First ever call — always report and initialize lastReported. SPEC §12.5.
    if (!state.everReported) {
        state.lastReported = value;
        state.everReported = true;
        return true;
    }
    float diff = value - state.lastReported;
    if (diff < 0.0f) diff = -diff;
    if (diff > state.cfg.deadband) {
        state.lastReported = value;
        return true;
    }
    return false;
}

bool SensorPipeline::applyHyst(float value, ChannelState& state) {
    if (state.cfg.hyst <= 0.0f) return state.hystActive; // hysteresis disabled
    float hi = state.cfg.threshold + state.cfg.hyst;
    float lo = state.cfg.threshold - state.cfg.hyst;
    if (!state.hystActive && value >= hi) {
        state.hystActive = true;
    } else if (state.hystActive && value <= lo) {
        state.hystActive = false;
    }
    return state.hystActive;
}

// ===========================================================================
// Private — JSON helpers (allocation-light; no JSON lib on device)
//
// All helpers search for key:value pairs in a JSON object string.  They are
// intentionally minimal: they handle the known automatica config schema shapes
// (simple number/string fields + one-level nested objects + arrays).
// ===========================================================================

// skipWhitespace — advance index past spaces/tabs/newlines/CR.
static size_t skipWS(const std::string& s, size_t i) {
    while (i < s.size() && (s[i] == ' ' || s[i] == '\t' ||
                              s[i] == '\n' || s[i] == '\r')) ++i;
    return i;
}

// findKey — locate `"<key>":` in src starting at `start`. Returns the index
// of the character right after the colon, or std::string::npos if not found.
static size_t findKey(const std::string& src, const std::string& key, size_t start = 0) {
    std::string needle = "\"" + key + "\"";
    for (;;) {
        size_t pos = src.find(needle, start);
        if (pos == std::string::npos) return std::string::npos;
        size_t after = skipWS(src, pos + needle.size());
        if (after < src.size() && src[after] == ':') return after + 1;
        start = pos + 1;
    }
}

std::string SensorPipeline::jsonFindString(const std::string& src,
                                            const std::string& key, bool& found) {
    found = false;
    size_t pos = findKey(src, key);
    if (pos == std::string::npos) return "";
    pos = skipWS(src, pos);
    if (pos >= src.size() || src[pos] != '"') return "";
    ++pos; // skip opening quote
    std::string out;
    while (pos < src.size() && src[pos] != '"') {
        if (src[pos] == '\\' && pos + 1 < src.size()) { ++pos; } // skip escape
        out += src[pos++];
    }
    found = true;
    return out;
}

float SensorPipeline::jsonFindFloat(const std::string& src,
                                     const std::string& key, bool& found) {
    found = false;
    size_t pos = findKey(src, key);
    if (pos == std::string::npos) return 0.0f;
    pos = skipWS(src, pos);
    if (pos >= src.size()) return 0.0f;
    // Read the number token.
    size_t start = pos;
    if (src[pos] == '-') ++pos;
    while (pos < src.size() && (src[pos] == '.' || (src[pos] >= '0' && src[pos] <= '9') ||
                                  src[pos] == 'e' || src[pos] == 'E' ||
                                  src[pos] == '+' || src[pos] == '-')) ++pos;
    if (pos == start) return 0.0f;
    found = true;
    return static_cast<float>(atof(src.substr(start, pos - start).c_str()));
}

int SensorPipeline::jsonFindInt(const std::string& src,
                                 const std::string& key, bool& found) {
    found = false;
    size_t pos = findKey(src, key);
    if (pos == std::string::npos) return 0;
    pos = skipWS(src, pos);
    if (pos >= src.size()) return 0;
    size_t start = pos;
    if (src[pos] == '-') ++pos;
    while (pos < src.size() && src[pos] >= '0' && src[pos] <= '9') ++pos;
    if (pos == start) return 0;
    found = true;
    return atoi(src.substr(start, pos - start).c_str());
}

// findMatchingBrace — find the closing brace/bracket for the opener at `open`.
// `opener` is '{' or '['; `closer` is '}' or ']'. Handles nesting + string skipping.
static size_t findMatching(const std::string& s, size_t open, char opener, char closer) {
    int depth = 0;
    for (size_t i = open; i < s.size(); ++i) {
        char c = s[i];
        if (c == '"') {
            // skip string literal
            ++i;
            while (i < s.size() && s[i] != '"') {
                if (s[i] == '\\') ++i; // skip escaped char
                ++i;
            }
            continue;
        }
        if (c == opener) ++depth;
        if (c == closer) { if (--depth == 0) return i; }
    }
    return std::string::npos;
}

std::string SensorPipeline::jsonFindObject(const std::string& src,
                                            const std::string& key, bool& found) {
    found = false;
    size_t pos = findKey(src, key);
    if (pos == std::string::npos) return "";
    pos = skipWS(src, pos);
    if (pos >= src.size() || src[pos] != '{') return "";
    size_t close = findMatching(src, pos, '{', '}');
    if (close == std::string::npos) return "";
    found = true;
    return src.substr(pos, close - pos + 1);
}

std::string SensorPipeline::jsonFindArray(const std::string& src,
                                           const std::string& key, bool& found) {
    found = false;
    size_t pos = findKey(src, key);
    if (pos == std::string::npos) return "";
    pos = skipWS(src, pos);
    if (pos >= src.size() || src[pos] != '[') return "";
    size_t close = findMatching(src, pos, '[', ']');
    if (close == std::string::npos) return "";
    found = true;
    return src.substr(pos, close - pos + 1);
}

// ===========================================================================
// Private — config parse helpers
// ===========================================================================

void SensorPipeline::parseCalTable(const std::string& arr, ChannelConfig& cfg) {
    // arr is a JSON array like: [{"raw":0,"calibrated":0},{"raw":100,"calibrated":98}]
    // Walk the array body (skip the outer '[' ']').
    cfg.calTable.clear();
    if (arr.size() < 2) return;
    std::string body = arr.substr(1, arr.size() - 2); // strip [ ]
    size_t pos = 0;
    while (pos < body.size()) {
        pos = skipWS(body, pos);
        if (pos >= body.size()) break;
        if (body[pos] != '{') { ++pos; continue; }
        size_t close = findMatching(body, pos, '{', '}');
        if (close == std::string::npos) break;
        std::string obj = body.substr(pos, close - pos + 1);
        bool fr = false, fc = false;
        float raw = jsonFindFloat(obj, "raw", fr);
        float cal = jsonFindFloat(obj, "calibrated", fc);
        if (fr && fc) {
            CalPoint pt; pt.raw = raw; pt.calibrated = cal;
            cfg.calTable.push_back(pt);
        }
        pos = close + 1;
        // skip comma
        pos = skipWS(body, pos);
        if (pos < body.size() && body[pos] == ',') ++pos;
    }
}

void SensorPipeline::parseChannelObject(const std::string& obj, ChannelConfig& cfg) {
    bool found = false;

    float v = jsonFindFloat(obj, "emaAlpha", found);
    if (found && v > 0.0f && v <= 1.0f) cfg.emaAlpha = v;

    int iv = jsonFindInt(obj, "medianWindow", found);
    if (found && (iv == 1 || iv == 3)) cfg.medianWindow = iv;

    v = jsonFindFloat(obj, "deadband", found);
    if (found && v >= 0.0f) cfg.deadband = v;

    v = jsonFindFloat(obj, "threshold", found);
    if (found) cfg.threshold = v;

    v = jsonFindFloat(obj, "hyst", found);
    if (found && v >= 0.0f) cfg.hyst = v;

    v = jsonFindFloat(obj, "calScale", found);
    if (found) cfg.calScale = v;

    v = jsonFindFloat(obj, "calOffset", found);
    if (found) cfg.calOffset = v;

    std::string arr = jsonFindArray(obj, "calTable", found);
    if (found) parseCalTable(arr, cfg);
}

// iterateChannelsObject — walk key:value pairs in the "channels" object.
// Each value is itself an object describing one channel's config. SPEC §12.8.
void SensorPipeline::iterateChannelsObject(const std::string& channelsObj) {
    // channelsObj looks like: {"temp":{...},"humidity":{...}}
    // Skip the outer braces.
    if (channelsObj.size() < 2) return;
    std::string body = channelsObj.substr(1, channelsObj.size() - 2);

    size_t pos = 0;
    while (pos < body.size()) {
        pos = skipWS(body, pos);
        if (pos >= body.size()) break;
        if (body[pos] != '"') { ++pos; continue; }

        // Read the channel name string.
        ++pos; // skip opening "
        size_t nameStart = pos;
        while (pos < body.size() && body[pos] != '"') ++pos;
        if (pos >= body.size()) break;
        std::string chName = body.substr(nameStart, pos - nameStart);
        ++pos; // skip closing "

        // Skip ":"
        pos = skipWS(body, pos);
        if (pos >= body.size() || body[pos] != ':') break;
        ++pos;
        pos = skipWS(body, pos);

        // Find the value object.
        if (pos >= body.size() || body[pos] != '{') { ++pos; continue; }
        size_t close = findMatching(body, pos, '{', '}');
        if (close == std::string::npos) break;
        std::string chObj = body.substr(pos, close - pos + 1);
        pos = close + 1;

        // Parse the channel config object and register it.
        ChannelConfig cfg;
        if (hasChannel(chName)) {
            // Preserve existing config as baseline for partial updates.
            for (size_t i = 0; i < channels_.size(); ++i) {
                if (channels_[i].name == chName) { cfg = channels_[i].cfg; break; }
            }
        }
        parseChannelObject(chObj, cfg);
        setChannel(chName, cfg);

        // Skip comma between entries.
        pos = skipWS(body, pos);
        if (pos < body.size() && body[pos] == ',') ++pos;
    }
}

}  // namespace automatica
