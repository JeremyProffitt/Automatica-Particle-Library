// SensorPipeline.h — Device-OS-free sensor smoothing, calibration, and report-gating
// engine. Operates on values a sketch has already read from hardware (no hardware seam).
//
// Pipeline per channel (applied in order):
//   1. Calibration  — linear (scale*raw + offset) or multi-point piecewise-linear table.
//   2. EMA filter   — exponential moving average: out = alpha*new + (1-alpha)*prev.
//                     alpha in (0.0, 1.0]; alpha=1.0 = no smoothing.
//   3. Median filter — 3-sample window (stores previous 2 samples). Eliminates spikes.
//   4. Deadband     — suppress a new report when |new - lastReported| < deadband.
//   5. Hysteresis   — for binary/threshold sensors: armed by crossing threshold+hyst,
//                     de-armed by crossing threshold-hyst; prevents relay chatter.
//
// Any filter may be disabled individually (alpha=1.0 = EMA off; medianLen=0 = median off;
// deadband=0 = deadband off; hyst=0 = hysteresis off).
//
// Config arrives two ways:
//   a. Direct C++ — call setChannel() before the first process() call.
//   b. JSON blob via LedgerConfig — call parseConfig(rawJson). See SPEC §12 for the
//      exact JSON schema. Parsing is allocation-light (no heap beyond std::string fields
//      already carried by ChannelConfig).
//
// Usage sketch:
//   SensorPipeline pipeline;
//   SensorPipeline::ChannelConfig cfg;
//   cfg.emaAlpha   = 0.2f;   // heavy smoothing
//   cfg.deadband   = 0.5f;   // ignore sub-0.5-unit changes
//   cfg.calScale   = 1.02f;  cfg.calOffset = -1.5f;  // linear calibration
//   pipeline.setChannel("temp", cfg);
//   // in loop():
//   automatica::Result r = pipeline.process("temp", rawDegC, nowMs);
//   if (r.shouldReport) reportState(r.value);
//
// Device-OS-free: only <string>, <vector>, <cstdlib>, <cstring>, <cstdint>, <cmath>.
// No Arduino headers — compiles clean on the host Catch2 build. SPEC §12.
#ifndef AUTOMATICA_SENSOR_PIPELINE_H
#define AUTOMATICA_SENSOR_PIPELINE_H

#include <string>
#include <vector>
#include <cstdint>

namespace automatica {

// CalPoint — one entry in a multi-point calibration table. Maps a raw sensor value
// to a calibrated (real-world) value via piecewise-linear interpolation between
// adjacent points. SPEC §12.2.
//
// raw       — the raw sensor reading at this calibration point. Units: same as the
//             raw value fed to process(). Range: any finite float.
// calibrated — the known-good (calibrated) value at this point. Units: same as the
//              processed output. Range: any finite float.
struct CalPoint {
    float raw;        // raw sensor value at this calibration anchor
    float calibrated; // corresponding calibrated (real-world) value
};

// ChannelConfig — per-channel configuration for the SensorPipeline. SPEC §12.1.
//
// All fields have safe defaults (no filtering, no calibration, no deadband/hysteresis)
// so a default-constructed ChannelConfig is a transparent pass-through.
struct ChannelConfig {
    // ---- EMA (exponential moving average) ----------------------------------------
    // emaAlpha — smoothing factor α in (0.0, 1.0]. Formula: out = α*new + (1-α)*prev.
    //   α = 1.0  → no smoothing (pass-through, default).
    //   α = 0.1  → heavy smoothing (slow to track rapid changes).
    //   α = 0.5  → moderate smoothing.
    //   Range: (0.0, 1.0]. Units: dimensionless. SPEC §12.3.
    float emaAlpha;

    // ---- Median filter -----------------------------------------------------------
    // medianWindow — number of samples in the sliding median window (1 or 3).
    //   1  → no median filtering (pass-through, default).
    //   3  → 3-sample window: store the two previous samples and take the median of
    //          [prev2, prev1, current]. Eliminates single-sample spikes.
    //   Range: 1 or 3. Units: dimensionless (sample count). SPEC §12.4.
    int medianWindow;

    // ---- Deadband ---------------------------------------------------------------
    // deadband — minimum change (|new - lastReported|) required to set shouldReport.
    //   0.0  → no deadband (every change triggers a report, default).
    //   > 0  → suppress reports for sub-threshold changes (avoids cloud spam on jitter).
    //   Range: [0.0, ∞). Units: same as the processed output value. SPEC §12.5.
    float deadband;

    // ---- Hysteresis (for binary / threshold sensors) ----------------------------
    // threshold — the value at which the binary output flips (e.g. a motion threshold).
    //   Only meaningful when hyst > 0.0. Units: same as processed value. SPEC §12.6.
    float threshold;

    // hyst — hysteresis band half-width around threshold.
    //   0.0  → hysteresis disabled (default).
    //   > 0  → output flips to HIGH when value crosses threshold+hyst from below;
    //           flips to LOW when value crosses threshold-hyst from above. Prevents
    //           chatter (relay bounce) near the threshold. Units: same as processed value.
    //   Range: [0.0, ∞). SPEC §12.6.
    float hyst;

    // ---- Linear calibration -----------------------------------------------------
    // calScale  — multiplicative gain applied to the raw value. Range: any float ≠ 0.
    //   1.0  → no gain (default). Units: dimensionless (calibrated/raw). SPEC §12.2.
    float calScale;

    // calOffset — additive offset applied after calScale: cal = calScale*raw + calOffset.
    //   0.0  → no offset (default). Units: same as the calibrated output. SPEC §12.2.
    float calOffset;

    // ---- Multi-point calibration ------------------------------------------------
    // calTable — optional piecewise-linear calibration table (overrides linear cal when
    //   non-empty). Points must be sorted ascending by CalPoint::raw. SPEC §12.2.
    //   Max 8 points (allocation-light). If empty, the linear cal (calScale/calOffset) is used.
    std::vector<CalPoint> calTable;

    // Default constructor — all filters disabled (transparent pass-through). SPEC §12.1.
    ChannelConfig()
        : emaAlpha(1.0f), medianWindow(1), deadband(0.0f),
          threshold(0.0f), hyst(0.0f), calScale(1.0f), calOffset(0.0f) {}
};

// Result — the output of SensorPipeline::process(). SPEC §12.7.
struct Result {
    // shouldReport — true when the processed value differs from the last reported value
    //   by more than the configured deadband (or when first called for this channel).
    //   When false, the caller should NOT publish to the cloud. SPEC §12.7.
    bool  shouldReport;

    // value — the calibrated, filtered, deadband-gated output value. Always valid even
    //   when shouldReport == false (represents the current best estimate). SPEC §12.7.
    float value;

    // hystActive — current hysteresis output state (true = above threshold+hyst,
    //   false = below threshold-hyst). Only meaningful when hyst > 0. SPEC §12.6.
    bool  hystActive;
};

// SensorPipeline — multi-channel sensor processing engine. SPEC §12.
//
// Thread safety: none — single-threaded use only (same as the rest of the firmware engine).
// Heap: allocates one ChannelState per named channel (first process() call for that name).
// Per-channel footprint: ~64 B (two floats EMA+median state + config refs + flags).
class SensorPipeline {
public:
    // SensorPipeline — construct with no channels. Channels are registered lazily
    // (on the first process() or setChannel() call for a given name). SPEC §12.
    SensorPipeline();

    // setChannel — configure (or reconfigure) a named channel. Must be called before
    //   the first process() for that channel, or to update config at runtime (e.g. when
    //   a new LedgerConfig config blob arrives). Resets the channel's filter state.
    // name — channel identifier, e.g. "temp", "humidity", "co2". Max 32 chars.
    // cfg  — filter + calibration parameters (see ChannelConfig). SPEC §12.1.
    void setChannel(const std::string& name, const ChannelConfig& cfg);

    // process — run the calibration → EMA → median → deadband/hysteresis pipeline
    //   for channel `name` and return whether the caller should publish the result.
    // name      — channel identifier; must match the name passed to setChannel().
    //             If no setChannel() was called for this name, a default pass-through
    //             config is used (emaAlpha=1, no deadband, no cal).
    // rawValue  — the raw hardware reading (before calibration/filtering). Range: any
    //             finite float. Units: hardware-dependent (same as calTable raw axis).
    // nowMs     — current time in milliseconds (millis() on device). Used internally
    //             for future rate-limiting extensions; passed for API stability. SPEC §12.
    // Returns Result with shouldReport + processed value + hystActive. SPEC §12.7.
    Result process(const std::string& name, float rawValue, unsigned long nowMs);

    // parseConfig — parse a LedgerConfig JSON blob and update all channels mentioned
    //   in it. Unknown fields are ignored (forward-compatible). SPEC §12.8 config schema.
    //
    // Expected JSON shape (SPEC §12.8):
    //   {
    //     "sensorPipeline": {
    //       "channels": {
    //         "<name>": {
    //           "emaAlpha":     <float 0..1>,
    //           "medianWindow": <int 1 or 3>,
    //           "deadband":     <float >=0>,
    //           "threshold":    <float>,
    //           "hyst":         <float >=0>,
    //           "calScale":     <float>,
    //           "calOffset":    <float>,
    //           "calTable": [ {"raw":<float>, "calibrated":<float>}, ... ]
    //         }
    //       }
    //     }
    //   }
    //
    // Returns true on successful parse of at least one channel; false on malformed JSON
    // or missing "sensorPipeline" key (existing channel configs left unchanged). SPEC §12.8.
    bool parseConfig(const std::string& rawJson);

    // channelCount — number of registered channels (for test introspection). SPEC §12.
    // Range: [0, ∞). Units: count.
    size_t channelCount() const;

    // hasChannel — true if a channel with `name` has been registered. SPEC §12.
    bool hasChannel(const std::string& name) const;

private:
    // ChannelState — runtime state for one channel. SPEC §12.
    struct ChannelState {
        std::string   name;          // channel identifier (matches the map key)
        ChannelConfig cfg;           // current configuration

        // EMA state
        float         emaPrev;       // previous EMA output. Units: same as processed value.
        bool          emaInitialized;// true after the first process() call for this channel.

        // Median filter state (3-sample window)
        float         medBuf[3];     // circular buffer of the last 3 raw-calibrated values.
        int           medCount;      // number of valid samples in medBuf (0, 1, or 2 at init).

        // Deadband state
        float         lastReported;  // last value that was reported (shouldReport=true).
        bool          everReported;  // true after the first shouldReport=true result.

        // Hysteresis state
        bool          hystActive;    // current hysteresis output (true = above threshold).

        ChannelState()
            : emaPrev(0.0f), emaInitialized(false),
              medCount(0),
              lastReported(0.0f), everReported(false),
              hystActive(false) {
            medBuf[0] = medBuf[1] = medBuf[2] = 0.0f;
        }
    };

    // ---- private methods -------------------------------------------------------

    // findOrCreate — return a reference to the ChannelState for `name`, creating a
    //   default-config one if it does not yet exist. SPEC §12.
    ChannelState& findOrCreate(const std::string& name);

    // applyCal — apply the calibration step (multi-point table if non-empty, else linear).
    // raw — uncalibrated sensor reading. Returns calibrated value. SPEC §12.2.
    static float applyCal(float raw, const ChannelConfig& cfg);

    // applyEma — one EMA step. Updates state.emaPrev. Returns smoothed value. SPEC §12.3.
    static float applyEma(float calValue, ChannelState& state);

    // applyMedian — 3-sample median (or pass-through when medianWindow != 3).
    //   Updates state.medBuf/medCount. Returns median value. SPEC §12.4.
    static float applyMedian(float emaValue, ChannelState& state);

    // applyDeadband — sets shouldReport based on the |value - lastReported| vs deadband.
    //   Updates state.lastReported on first-ever call (regardless of deadband). SPEC §12.5.
    static bool applyDeadband(float value, ChannelState& state);

    // applyHyst — update state.hystActive if hyst > 0; always returns state.hystActive.
    //   No deadband interaction — hysteresis operates on the filtered value. SPEC §12.6.
    static bool applyHyst(float value, ChannelState& state);

    // ---- hand-rolled JSON helpers (no JSON lib on device) ----------------------

    // jsonFindKey — find the string value associated with `key` in a flat JSON object
    //   `src`. Returns "" + sets found=false if absent. The search is positional and
    //   good enough for the known config schema — not a full JSON parser.
    static std::string jsonFindString(const std::string& src, const std::string& key,
                                       bool& found);

    // jsonFindFloat — extract a float value for `key`. Sets found=false if absent.
    static float jsonFindFloat(const std::string& src, const std::string& key, bool& found);

    // jsonFindInt — extract an int value for `key`. Sets found=false if absent.
    static int   jsonFindInt(const std::string& src, const std::string& key, bool& found);

    // jsonFindObject — return the substring of `src` that is the value object for `key`
    //   (including its braces). Returns "" + found=false if absent. SPEC §12.8.
    static std::string jsonFindObject(const std::string& src, const std::string& key,
                                       bool& found);

    // jsonFindArray — return the substring of `src` that is the value array for `key`
    //   (including its brackets). Returns "" + found=false if absent. SPEC §12.8.
    static std::string jsonFindArray(const std::string& src, const std::string& key,
                                      bool& found);

    // parseChannelObject — parse a single channel JSON object into cfg. SPEC §12.8.
    static void parseChannelObject(const std::string& obj, ChannelConfig& cfg);

    // parseCalTable — parse the "calTable" JSON array into cfg.calTable. SPEC §12.2.
    static void parseCalTable(const std::string& arr, ChannelConfig& cfg);

    // iterateChannelsObject — iterate the key:"<name>", value:{...} pairs in the
    //   "channels" JSON object and call setChannel for each. SPEC §12.8.
    void iterateChannelsObject(const std::string& channelsObj);

    // ---- members ---------------------------------------------------------------

    std::vector<ChannelState> channels_; // registered channel states (append-only)
};

}  // namespace automatica

#endif  // AUTOMATICA_SENSOR_PIPELINE_H
