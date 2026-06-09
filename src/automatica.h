// automatica.h — public, Device-OS-free API for the automatica Particle library
// (SPEC v2). Users declare endpoints with instance-aware capabilities (power,
// range, mode, toggle), attach a control callback, then drive begin()/loop().
//
// The whole core is std::string/int/std::vector only (no Particle.h, no Arduino
// String) so contract/fixtures-driven Catch2 tests build and run on the host. The
// device-cloud wire is a bit-packed binary blob wrapped in Ascii85 (SPEC §1); the
// on-device cloud wiring lives behind CloudPort (CloudPort.h / AutomaticaCloud.h).
#ifndef AUTOMATICA_H
#define AUTOMATICA_H

#include <cstddef>
#include <string>
#include <vector>

#include "CloudPort.h"

namespace automatica {

// ---- Contract constants (SPEC.md §1) ---------------------------------------

// Max Ascii85 chars per manifest content page (Gen2 Particle.variable cap, §1.4).
static const size_t kPageSizeLimitChars = 864;

// Current device-cloud schema version (the `ver` byte in automaticaManifest0).
static const int kSchemaVersion = 2;

// Minimum publish gap for automaticaState (≤1 publish/sec per device, §1.8).
static const unsigned long kPublishMinIntervalMs = 1000;

// 0xFF wire sentinel for a singleton capability instance (SPEC §1.5).
static const int kNoInstance = -1;

// Capability codes (SPEC §1.3). v2 implements o/b/c (singleton) + r/m/t (instanced).
static const char kCapPower            = 'o';
static const char kCapBrightness       = 'b';
static const char kCapColor            = 'c';
static const char kCapColorTemperature = 'k';
static const char kCapPercentage       = 'p';
static const char kCapLock             = 'l';
static const char kCapRange            = 'r';
static const char kCapMode             = 'm';
static const char kCapToggle           = 't';
static const char kCapContactSensor    = 'd';  // Alexa.ContactSensor (read-only)
static const char kCapMotionSensor     = 'v';  // Alexa.MotionSensor (read-only)
static const char kCapTemperatureSensor = 'e'; // Alexa.TemperatureSensor (read-only)
static const char kCapThermostat       = 'h';  // Alexa.ThermostatController
static const char kCapScene            = 's';  // Alexa.SceneController (momentary)
static const char kCapSecurityPanel    = 'a';  // Alexa.SecurityPanelController (armState)
static const char kCapSpeaker          = 'u';  // Alexa.Speaker (volume + muted)
static const char kCapPlayback         = 'y';  // Alexa.PlaybackController (momentary)
static const char kCapPowerLevel       = 'w';  // Alexa.PowerLevelController — 0..100 (int)
static const char kCapStepSpeaker      = 'g';  // Alexa.StepSpeaker — AdjustVolume/SetMute (momentary)
static const char kCapTimeHold         = 'i';  // Alexa.TimeHoldController — Hold/Resume (momentary)
static const char kCapInput            = 'j';  // Alexa.InputController — SelectInput (stores input INDEX)
static const char kCapHumidity         = 'n';  // read-only humidity % (0..100); Lambda renders as RangeController "Humidity"
static const char kCapChannel          = 'f';  // Alexa.ChannelController — ChangeChannel/SkipChannels (state = channel number)
static const char kCapEqualizer        = 'q';  // Alexa.EqualizerController — bass/mid/treble + mode (sub-op wire)
static const char kCapEventDetection   = 'x';  // Alexa.EventDetectionSensor — humanPresenceDetectionState (read-only bool, proactive)
static const char kCapDoorbell         = 'z';  // Alexa.DoorbellEventSource — bool "pressed" drives a DoorbellPress event (proactive)
static const char kCapCamera           = 'C';  // Alexa.CameraStreamController — stateless singleton; Lambda renders stream config + InitializeCameraStreams snapshot imageUri (SPEC §1.3 capEnum 27)

// Thermostat control sub-ops (SPEC §1.6).
static const int kThermoSubSetpoint = 0;
static const int kThermoSubMode     = 1;

// StepSpeaker control sub-ops (SPEC §1.6): cmd.sub selects the op.
static const int kStepVolumeSub = 0; // AdjustVolume: cmd.intVal = signed volume steps
static const int kStepMuteSub   = 1; // SetMute:      cmd.boolVal = mute

// ChannelController control sub-ops (SPEC §1.6): cmd.sub selects the op; both carry i16.
static const int kChannelSubChange = 0; // ChangeChannel: cmd.intVal = absolute channel number
static const int kChannelSubSkip   = 1; // SkipChannels:  cmd.intVal = signed channel count (relative)

// EqualizerController control sub-ops (SPEC §1.6): cmd.sub selects the op.
static const int kEqualizerSubBands = 0; // SetBands (absolute): cmd.bass/mid/treble
static const int kEqualizerSubMode  = 1; // SetMode: cmd.mode (MOVIE/MUSIC/NIGHT/SPORT/TV)

// Equalizer band level range (SPEC §2.1).
static const int kEqBandMin = -6;
static const int kEqBandMax =  6;

// State value types (SPEC §1.5 valType).
enum ValType {
    VAL_BOOL = 0, VAL_INT = 1, VAL_MODE_INDEX = 2, VAL_COLOR = 3,
    VAL_TEMP = 4, VAL_THERMOSTAT = 5, VAL_ARMSTATE = 6, VAL_SPEAKER = 7,
    VAL_EQUALIZER = 8
};

// automaticaCtl return status enum (SPEC §1.6). 0 = OK, negatives = errors.
enum CtlStatus {
    CTL_OK               =  0,
    CTL_ERR_PARSE        = -1,  // bad Ascii85 / truncated blob / unknown capEnum
    CTL_ERR_BAD_INDEX    = -2,  // no such endpoint
    CTL_ERR_BAD_CODE     = -3,  // capability not declared on endpoint
    CTL_ERR_OUT_OF_RANGE = -4,  // value out of range / not a valid mode
    CTL_ERR_CALLBACK     = -5,  // user handler returned failure
    CTL_ERR_BAD_INSTANCE = -6   // capability present but not at that instance
};

// ---- Capability config + state ---------------------------------------------

// Semantics action/state mapping (SPEC §1.7). Action is "Open"/"Close"/"Raise"/
// "Lower"; directive is "SetRangeValue"/"AdjustRangeValue"/"SetMode".
struct ActionMapping {
    std::string action;
    std::string directive;
    int         value;     // rangeValue or delta
    std::string valueStr;  // mode value (SetMode)
    ActionMapping() : value(0) {}
};

// State mapping: kind 0 StatesToValue(int a); 1 StatesToRange(a..b);
// 2 StatesToValue(string valueStr) (SPEC §1.7).
struct StateMapping {
    std::string state;
    int         kind;
    int         a;
    int         b;
    std::string valueStr;
    StateMapping() : kind(0), a(0), b(0) {}
};

struct Semantics {
    std::vector<ActionMapping> actionMappings;
    std::vector<StateMapping>  stateMappings;
};

struct Preset {
    int                      value;
    std::vector<std::string> resources;
    Preset() : value(0) {}
};

struct RangeConfig {
    int                      min;
    int                      max;
    int                      precision;
    std::string              unit;       // catalog name ("" = none), e.g. "Percent"
    std::vector<std::string> resources;
    std::vector<Preset>      presets;
    bool                     hasSemantics;
    Semantics                semantics;
    RangeConfig() : min(0), max(0), precision(0), hasSemantics(false) {}
};

struct ModeOption {
    std::string              value;
    std::vector<std::string> resources;
};

struct ModeConfig {
    bool                     ordered;
    std::vector<ModeOption>  modes;
    std::vector<std::string> resources;
    bool                     hasSemantics;
    Semantics                semantics;
    ModeConfig() : ordered(false), hasSemantics(false) {}
};

struct ToggleConfig {
    std::vector<std::string> resources;
    bool                     hasSemantics;
    Semantics                semantics;
    ToggleConfig() : hasSemantics(false) {}
};

// CapState is the current state of one capability instance. The populated field
// depends on the capability code (SPEC §1.5).
struct CapState {
    bool        b;        // o/l/t/d/v (bool)
    int         i;        // b/k/p/r (int)
    std::string mode;     // m (mode value)
    int         hue;      // c
    int         sat;      // c
    int         tempDeci; // e (temperature, tenths of a degree)
    std::string scale;    // e (CELSIUS | FAHRENHEIT | KELVIN)
    int         bass;     // q equalizer BASS level (kEqBandMin..kEqBandMax)
    int         mid;      // q equalizer MIDRANGE level
    int         treble;   // q equalizer TREBLE level
    CapState() : b(false), i(0), hue(0), sat(0), tempDeci(0), bass(0), mid(0), treble(0) {}
};

// Capability is one declared capability on an endpoint. For instanced caps
// (r/m/t) `instance` is the Alexa instance NAME and `instanceIdx` the compact wire
// index (assigned in declaration order by the manifest builder).
struct Capability {
    char         code;
    std::string  instance;     // Alexa instance name (instanced caps)
    int          instanceIdx;  // wire index, kNoInstance for singletons
    RangeConfig  range;
    ModeConfig   mode;
    ToggleConfig toggle;
    bool         hasState;
    CapState     state;
    Capability() : code(0), instanceIdx(kNoInstance), hasState(false) {}
};

struct Endpoint {
    std::string              id;     // ^[a-z0-9_-]{1,24}$
    std::string              name;   // Alexa friendlyName
    std::string              desc;   // Alexa description
    std::vector<std::string> cat;    // Alexa displayCategories, e.g. {"FAN"}
    std::vector<Capability>  caps;

    bool hasCode(char code) const;
};

// ---- Control command (parsed automaticaCtl arg) -----------------------------

// Result of decoding+validating an automaticaCtl arg. On CTL_OK the typed fields
// for the matching code are populated.
struct CtlCommand {
    int         status;
    int         idx;
    char        code;
    int         instance;  // wire index, kNoInstance for singletons
    bool        boolVal;   // o/l/t
    int         intVal;    // b/k/p/r
    int         hue;       // c
    int         sat;       // c
    int         bri;       // c
    std::string mode;      // m, and thermostat (h) mode value
    int         sub;       // sub-op (kThermoSub* / kStep* / kChannel* / kEqualizerSub*)
    int         tempDeci;  // thermostat setpoint (tenths of a degree)
    std::string scale;     // thermostat scale
    int         bass;      // q equalizer SetBands levels
    int         mid;
    int         treble;
    CtlCommand() : status(CTL_ERR_PARSE), idx(-1), code(0), instance(kNoInstance),
                   boolVal(false), intVal(0), hue(0), sat(0), bri(0), sub(0), tempDeci(0),
                   bass(0), mid(0), treble(0) {}
};

typedef bool (*ControlHandler)(const CtlCommand& cmd, void* ctx);

// ---- Manifest pages ---------------------------------------------------------

// The paged manifest: page0 is the Ascii85 4-byte header; content holds
// automaticaManifest1..N (each ≤ kPageSizeLimitChars). Concatenating content and
// Ascii85-decoding yields the packed endpoints blob (SPEC §1.4/§1.5).
struct ManifestPages {
    std::string              page0;
    std::vector<std::string> content;
};

// ---- Free functions: Ascii85, binary pack/parse (SPEC §1) -------------------

// Ascii85 (SPEC §1.1): Adobe charset, no framing, no z/y. Operate on raw bytes
// carried in std::string. ascii85Decode returns false on invalid input.
std::string ascii85Encode(const std::string& bytes);
bool        ascii85Decode(const std::string& s, std::string& out);

// Pack the endpoints array to the binary blob (SPEC §1.5). Assigns instanceIdx.
std::string packEndpoints(std::vector<Endpoint>& eps);

// Build the paged manifest: page0 header + Ascii85 content pages (SPEC §1.4).
ManifestPages buildManifest(std::vector<Endpoint>& eps);

// Pack the automaticaState payload for one endpoint (SPEC §1.8): binary then
// Ascii85, ready to publish.
std::string packStatePayload(int idx, const Endpoint& ep);

// Decode + validate an automaticaCtl arg (Ascii85 → binary → validate) against
// the endpoint model (SPEC §1.6). Returns a CtlCommand with status set; on CTL_OK
// the typed fields are populated. Does NOT invoke the user callback.
CtlCommand parseControl(const std::string& arg, const std::vector<Endpoint>& eps);

// ---- The Automatica facade --------------------------------------------------

class Automatica {
public:
    Automatica();

    void setCloudPort(CloudPort* port);

    // Register an endpoint; returns its idx (array index == Alexa idx). Order is
    // the device identity and must be stable across reboots.
    int addEndpoint(const std::string& id,
                    const std::string& name,
                    const std::string& desc,
                    const std::vector<std::string>& cat);

    // Capability builders. Each appends to endpoint `idx` and (for instanced caps)
    // returns the new capability's instance index; singletons return 0.
    //
    // SINGLETON builders (one per endpoint, instance == kNoInstance). Pass the
    // returned 0 nowhere — singletons are addressed by code in setInitialState /
    // the control callback (cmd.code), not by instance.
    int addPower(int idx);            // 'o' Alexa.PowerController — TurnOn/TurnOff (bool)
    int addBrightness(int idx);       // 'b' Alexa.BrightnessController — 0..100 (int)
    int addColor(int idx);            // 'c' Alexa.ColorController — hue 0..360, sat/bri 0..100
    int addColorTemperature(int idx); // 'k' Alexa.ColorTemperatureController — 1000..10000 K
    int addPercentage(int idx);       // 'p' Alexa.PercentageController — 0..100 (int)
    int addPowerLevel(int idx);       // 'w' Alexa.PowerLevelController — 0..100 (int)
    int addStepSpeaker(int idx);      // 'g' Alexa.StepSpeaker — AdjustVolume(steps)/SetMute (momentary)
    int addTimeHold(int idx);         // 'i' Alexa.TimeHoldController — Hold/Resume (bool: true=Hold, momentary)
    int addInput(int idx);            // 'j' Alexa.InputController — SelectInput (cmd.intVal = input index)
    int addLock(int idx);             // 'l' Alexa.LockController — Lock/Unlock (bool: true=LOCKED)
    int addContactSensor(int idx);    // 'd' Alexa.ContactSensor — read-only (bool: true=DETECTED)
    int addMotionSensor(int idx);     // 'v' Alexa.MotionSensor — read-only (bool: true=DETECTED)
    int addTemperatureSensor(int idx);// 'e' Alexa.TemperatureSensor — read-only (tempDeci + scale)
    int addHumiditySensor(int idx);   // 'n' read-only humidity % (0..100, int); Alexa.RangeController "Humidity"
    int addEventDetectionSensor(int idx); // 'x' Alexa.EventDetectionSensor — read-only (bool: true=DETECTED, proactive)
    int addDoorbell(int idx);         // 'z' Alexa.DoorbellEventSource — set bool true + reportState() to fire a DoorbellPress
    int addCamera(int idx);           // 'C' Alexa.CameraStreamController — stateless; advertises the endpoint as a CAMERA. Snapshot capture is out-of-band on automatica/<thing>/snapshot/request (see examples/camera-snapshot)
    int addThermostat(int idx);       // 'h' Alexa.ThermostatController — setpoint + mode
    int addScene(int idx);            // 's' Alexa.SceneController — momentary Activate/Deactivate
    int addSecurityPanel(int idx);    // 'a' Alexa.SecurityPanelController — armState
    int addSpeaker(int idx);          // 'u' Alexa.Speaker — volume 0..100 + muted (bool)
    int addPlayback(int idx);         // 'y' Alexa.PlaybackController — momentary transport ops
    int addChannel(int idx);          // 'f' Alexa.ChannelController — ChangeChannel/SkipChannels (cmd.sub + cmd.intVal)
    int addEqualizer(int idx);        // 'q' Alexa.EqualizerController — SetBands/SetMode (cmd.sub + cmd.bass/mid/treble | cmd.mode)

    // INSTANCED builders (many per endpoint, each a distinct named instance).
    int addRange(int idx, const std::string& instance, const RangeConfig& cfg);  // 'r'
    int addMode(int idx, const std::string& instance, const ModeConfig& cfg);    // 'm'
    int addToggle(int idx, const std::string& instance, const ToggleConfig& cfg);// 't'

    void onControl(ControlHandler handler, void* ctx = 0);

    // Set initial state for a capability (before begin()). For singletons pass the
    // code with instance kNoInstance; for instanced caps pass the wire instance
    // index returned by the add* builder.
    void setInitialState(int idx, char code, int instance, const CapState& st);

    // Build the manifest + register cloud variables/function. Call once from
    // setup() after all endpoints/capabilities are added.
    void begin();

    // Drive from loop(): flush debounced publishes and emit the initial snapshot.
    void loop();

    // Mark a capability's state changed (latest-wins, coalesced, ≤1/sec).
    void reportState(int idx);

    // ---- accessors (tests + the cloud function trampoline) ------------------
    size_t endpointCount() const;
    const Endpoint& endpoint(int idx) const;
    const ManifestPages& manifest() const;

    // Handle a raw automaticaCtl arg: decode, validate, invoke callback, apply.
    int handleControl(const std::string& arg);

private:
    int addSingleton_(int idx, char code);
    Capability* capAt(int idx, char code, int instance);
    void rebuildManifest();
    void flushPublishes();
    void emitInitialSnapshotIfReady();

    CloudPort*             port_;
    ControlHandler         handler_;
    void*                  handlerCtx_;
    std::vector<Endpoint>  endpoints_;
    ManifestPages          manifest_;

    struct PubSlot {
        bool        dirty;
        std::string pending;
        PubSlot() : dirty(false) {}
    };
    std::vector<PubSlot> pub_;

    bool          everPublished_;
    unsigned long lastPublishMs_;
    bool          begun_;
    bool          snapshotDone_;
};

}  // namespace automatica

#endif  // AUTOMATICA_H
