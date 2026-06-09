// automatica.cpp — Device-OS-free core (SPEC v2). Binary wire format + Ascii85,
// instance-aware capabilities. No Particle.h / Arduino String here; only
// std::string/std::vector/int so host Catch2 tests link. The byte layout MUST
// match the Go reference codec (lambda/internal/device/codec.go) and the fixtures.
#include "automatica.h"

#include <cstddef>

namespace automatica {

// ---------------------------------------------------------------------------
// Enum tables (SPEC §1.3) — switch-based to avoid <map> on the MCU.
// ---------------------------------------------------------------------------

static int capCharToEnum(char c) {
    switch (c) {
        case kCapPower: return 1; case kCapBrightness: return 2;
        case kCapColor: return 3; case kCapColorTemperature: return 4;
        case kCapPercentage: return 5; case kCapLock: return 6;
        case kCapRange: return 7; case kCapMode: return 8; case kCapToggle: return 9;
        case kCapContactSensor: return 10; case kCapMotionSensor: return 11;
        case kCapTemperatureSensor: return 12; case kCapThermostat: return 13;
        case kCapScene: return 14; case kCapSecurityPanel: return 15;
        case kCapSpeaker: return 16; case kCapPlayback: return 17;
        case kCapPowerLevel: return 18;
        case kCapStepSpeaker: return 19;
        case kCapTimeHold: return 20;
        case kCapInput: return 21;
        case kCapHumidity: return 22;
        case kCapChannel: return 23;
        case kCapEqualizer: return 24;
        case kCapEventDetection: return 25;
        case kCapDoorbell: return 26;
        case kCapCamera: return 27;
    }
    return 0;
}
static char capEnumToChar(int e) {
    switch (e) {
        case 1: return kCapPower; case 2: return kCapBrightness;
        case 3: return kCapColor; case 4: return kCapColorTemperature;
        case 5: return kCapPercentage; case 6: return kCapLock;
        case 7: return kCapRange; case 8: return kCapMode; case 9: return kCapToggle;
        case 10: return kCapContactSensor; case 11: return kCapMotionSensor;
        case 12: return kCapTemperatureSensor; case 13: return kCapThermostat;
        case 14: return kCapScene; case 15: return kCapSecurityPanel;
        case 16: return kCapSpeaker; case 17: return kCapPlayback;
        case 18: return kCapPowerLevel;
        case 19: return kCapStepSpeaker;
        case 20: return kCapTimeHold;
        case 21: return kCapInput;
        case 22: return kCapHumidity;
        case 23: return kCapChannel;
        case 24: return kCapEqualizer;
        case 25: return kCapEventDetection;
        case 26: return kCapDoorbell;
        case 27: return kCapCamera;
    }
    return 0;
}
static int armStateToEnum(const std::string& s) {
    if (s == "ARMED_AWAY") return 1; if (s == "ARMED_STAY") return 2;
    if (s == "ARMED_NIGHT") return 3; if (s == "DISARMED") return 4;
    return 4; // default DISARMED
}
static std::string armStateFromEnum(int e) {
    switch (e) { case 1: return "ARMED_AWAY"; case 2: return "ARMED_STAY"; case 3: return "ARMED_NIGHT"; case 4: return "DISARMED"; }
    return "DISARMED";
}

static int tempScaleToEnum(const std::string& s) {
    if (s == "CELSIUS") return 1; if (s == "FAHRENHEIT") return 2; if (s == "KELVIN") return 3;
    return 1; // default CELSIUS
}
static std::string tempScaleFromEnum(int e) {
    switch (e) { case 1: return "CELSIUS"; case 2: return "FAHRENHEIT"; case 3: return "KELVIN"; }
    return "CELSIUS";
}
static int thermoModeToEnum(const std::string& s) {
    if (s == "HEAT") return 1; if (s == "COOL") return 2; if (s == "AUTO") return 3;
    if (s == "OFF") return 4; if (s == "ECO") return 5;
    return 0; // unknown
}
static std::string thermoModeFromEnum(int e) {
    switch (e) { case 1: return "HEAT"; case 2: return "COOL"; case 3: return "AUTO"; case 4: return "OFF"; case 5: return "ECO"; }
    return "";
}

static int eqModeToEnum(const std::string& s) {
    if (s == "MOVIE") return 1; if (s == "MUSIC") return 2; if (s == "NIGHT") return 3;
    if (s == "SPORT") return 4; if (s == "TV") return 5;
    return 0; // custom (explicit bands)
}
static std::string eqModeFromEnum(int e) {
    switch (e) { case 1: return "MOVIE"; case 2: return "MUSIC"; case 3: return "NIGHT"; case 4: return "SPORT"; case 5: return "TV"; }
    return "";
}

static bool isInstanced(char code) {
    return code == kCapRange || code == kCapMode || code == kCapToggle;
}

// isMomentary reports codes that carry NO retrievable state — the control fires
// the callback but stores/publishes nothing (Scene, Playback, StepSpeaker). They
// must never be marked stateful, or reportState would pack a value-less entry.
static bool isMomentary(char code) {
    return code == kCapScene || code == kCapPlayback || code == kCapStepSpeaker || code == kCapTimeHold;
}

static int catNameToEnum(const std::string& n) {
    if (n == "LIGHT") return 1; if (n == "SWITCH") return 2;
    if (n == "SMARTPLUG") return 3; if (n == "OUTLET") return 4;
    if (n == "FAN") return 5; if (n == "THERMOSTAT") return 6;
    if (n == "TEMPERATURE_SENSOR") return 7; if (n == "CONTACT_SENSOR") return 8;
    if (n == "MOTION_SENSOR") return 9; if (n == "DOOR") return 10;
    if (n == "GARAGE_DOOR") return 11; if (n == "INTERIOR_BLIND") return 12;
    if (n == "EXTERIOR_BLIND") return 13; if (n == "CURTAIN") return 14;
    if (n == "SHADE") return 15; if (n == "LOCK") return 16;
    if (n == "SCENE_TRIGGER") return 17; if (n == "ACTIVITY_TRIGGER") return 18;
    if (n == "SPEAKER") return 19; if (n == "TV") return 20;
    if (n == "SECURITY_PANEL") return 21; if (n == "STREAMING_DEVICE") return 22;
    if (n == "CAMERA") return 23;
    if (n == "OTHER") return 255;
    return -1;
}
static std::string catEnumToName(int e) {
    switch (e) {
        case 1: return "LIGHT"; case 2: return "SWITCH"; case 3: return "SMARTPLUG";
        case 4: return "OUTLET"; case 5: return "FAN"; case 6: return "THERMOSTAT";
        case 7: return "TEMPERATURE_SENSOR"; case 8: return "CONTACT_SENSOR";
        case 9: return "MOTION_SENSOR"; case 10: return "DOOR"; case 11: return "GARAGE_DOOR";
        case 12: return "INTERIOR_BLIND"; case 13: return "EXTERIOR_BLIND";
        case 14: return "CURTAIN"; case 15: return "SHADE"; case 16: return "LOCK";
        case 17: return "SCENE_TRIGGER"; case 18: return "ACTIVITY_TRIGGER";
        case 19: return "SPEAKER"; case 20: return "TV";
        case 21: return "SECURITY_PANEL"; case 22: return "STREAMING_DEVICE";
        case 23: return "CAMERA"; case 255: return "OTHER";
    }
    return "";
}

static int unitNameToEnum(const std::string& n) {
    if (n.empty()) return 0;
    if (n == "Percent") return 1; if (n == "Angle.Degrees") return 2;
    if (n == "Temperature.Celsius") return 3; if (n == "Temperature.Fahrenheit") return 4;
    if (n == "Temperature.Kelvin") return 5; if (n == "Distance.Meters") return 6;
    if (n == "Distance.Feet") return 7;
    return 0;
}
static std::string unitEnumToName(int e) {
    switch (e) {
        case 1: return "Percent"; case 2: return "Angle.Degrees";
        case 3: return "Temperature.Celsius"; case 4: return "Temperature.Fahrenheit";
        case 5: return "Temperature.Kelvin"; case 6: return "Distance.Meters";
        case 7: return "Distance.Feet";
    }
    return "";
}

static int semActionToEnum(const std::string& a) {
    if (a == "Open") return 1; if (a == "Close") return 2;
    if (a == "Raise") return 3; if (a == "Lower") return 4;
    return 0;
}
static std::string semActionFromEnum(int e) {
    switch (e) { case 1: return "Open"; case 2: return "Close"; case 3: return "Raise"; case 4: return "Lower"; }
    return "";
}
static int semStateToEnum(const std::string& s) {
    if (s == "Open") return 1; if (s == "Closed") return 2; return 0;
}
static std::string semStateFromEnum(int e) {
    switch (e) { case 1: return "Open"; case 2: return "Closed"; } return "";
}
static int semDirToEnum(const std::string& d) {
    if (d == "SetRangeValue") return 1; if (d == "AdjustRangeValue") return 2;
    if (d == "SetMode") return 3; return 0;
}
static std::string semDirFromEnum(int e) {
    switch (e) { case 1: return "SetRangeValue"; case 2: return "AdjustRangeValue"; case 3: return "SetMode"; }
    return "";
}

// ---------------------------------------------------------------------------
// Ascii85 (SPEC §1.1)
// ---------------------------------------------------------------------------

std::string ascii85Encode(const std::string& b) {
    std::string out;
    out.reserve((b.size() + 3) / 4 * 5);
    for (size_t i = 0; i < b.size(); i += 4) {
        unsigned long v = 0;
        int k = 0;
        for (; k < 4 && i + k < b.size(); ++k) {
            v |= (unsigned long)(unsigned char)b[i + k] << (24 - 8 * k);
        }
        unsigned char digits[5];
        unsigned long t = v;
        for (int j = 4; j >= 0; --j) { digits[j] = (unsigned char)(t % 85); t /= 85; }
        for (int j = 0; j <= k; ++j) out.push_back((char)(33 + digits[j]));
    }
    return out;
}

bool ascii85Decode(const std::string& s, std::string& out) {
    out.clear();
    for (size_t i = 0; i < s.size(); i += 5) {
        unsigned long long v = 0;
        int m = 0;
        for (; m < 5 && i + m < s.size(); ++m) {
            unsigned char c = (unsigned char)s[i + m];
            if (c < 33 || c > 117) return false;
            v = v * 85 + (unsigned long long)(c - 33);
        }
        if (m == 1) return false;
        for (int j = m; j < 5; ++j) v = v * 85 + 84;
        if (v > 0xFFFFFFFFULL) return false;
        for (int j = 0; j < m - 1; ++j) out.push_back((char)((v >> (24 - 8 * j)) & 0xFF));
    }
    return true;
}

// ---------------------------------------------------------------------------
// Binary writer / reader
// ---------------------------------------------------------------------------

static void wU8(std::string& o, int v) { o.push_back((char)(v & 0xFF)); }
static void wU16(std::string& o, int v) { o.push_back((char)((v >> 8) & 0xFF)); o.push_back((char)(v & 0xFF)); }
static void wI16(std::string& o, int v) {
    unsigned short u = (unsigned short)(short)v;
    o.push_back((char)((u >> 8) & 0xFF));
    o.push_back((char)(u & 0xFF));
}
static void wStr(std::string& o, const std::string& s) { wU8(o, (int)s.size()); o += s; }

struct Reader {
    std::string b;
    size_t i;
    bool ok;
    explicit Reader(const std::string& bb) : b(bb), i(0), ok(true) {}
    int u8() {
        if (!ok || i >= b.size()) { ok = false; return 0; }
        return (unsigned char)b[i++];
    }
    int u16() {
        if (!ok || i + 2 > b.size()) { ok = false; return 0; }
        int v = ((unsigned char)b[i] << 8) | (unsigned char)b[i + 1];
        i += 2;
        return v;
    }
    int i16() { return (int)(short)(unsigned short)u16(); }
    std::string str() {
        int n = u8();
        if (!ok || i + (size_t)n > b.size()) { ok = false; return std::string(); }
        std::string s = b.substr(i, n);
        i += n;
        return s;
    }
};

// ---------------------------------------------------------------------------
// Endpoint helpers
// ---------------------------------------------------------------------------

bool Endpoint::hasCode(char code) const {
    for (size_t i = 0; i < caps.size(); ++i) if (caps[i].code == code) return true;
    return false;
}

static void assignInstanceIdx(std::vector<Endpoint>& eps) {
    for (size_t e = 0; e < eps.size(); ++e) {
        int next = 0;
        for (size_t c = 0; c < eps[e].caps.size(); ++c) {
            if (isInstanced(eps[e].caps[c].code)) eps[e].caps[c].instanceIdx = next++;
            else eps[e].caps[c].instanceIdx = kNoInstance;
        }
    }
}

// ---------------------------------------------------------------------------
// Pack (SPEC §1.5)
// ---------------------------------------------------------------------------

static void packSemantics(std::string& o, bool has, const Semantics& s) {
    if (!has) { wU8(o, 0); return; }
    wU8(o, 1);
    wU8(o, (int)s.actionMappings.size());
    for (size_t i = 0; i < s.actionMappings.size(); ++i) {
        const ActionMapping& a = s.actionMappings[i];
        wU8(o, semActionToEnum(a.action));
        wU8(o, semDirToEnum(a.directive));
        wI16(o, a.value);
        wStr(o, a.valueStr);
    }
    wU8(o, (int)s.stateMappings.size());
    for (size_t i = 0; i < s.stateMappings.size(); ++i) {
        const StateMapping& m = s.stateMappings[i];
        wU8(o, semStateToEnum(m.state));
        wU8(o, m.kind);
        wI16(o, m.a);
        wI16(o, m.b);
        wStr(o, m.valueStr);
    }
}

static std::string packCapConfig(const Capability& c) {
    std::string o;
    if (c.code == kCapRange) {
        wStr(o, c.instance);
        wI16(o, c.range.min);
        wI16(o, c.range.max);
        wI16(o, c.range.precision);
        wU8(o, unitNameToEnum(c.range.unit));
        wU8(o, (int)c.range.resources.size());
        for (size_t i = 0; i < c.range.resources.size(); ++i) wStr(o, c.range.resources[i]);
        wU8(o, (int)c.range.presets.size());
        for (size_t i = 0; i < c.range.presets.size(); ++i) {
            wI16(o, c.range.presets[i].value);
            wU8(o, (int)c.range.presets[i].resources.size());
            for (size_t j = 0; j < c.range.presets[i].resources.size(); ++j)
                wStr(o, c.range.presets[i].resources[j]);
        }
        packSemantics(o, c.range.hasSemantics, c.range.semantics);
    } else if (c.code == kCapMode) {
        wStr(o, c.instance);
        wU8(o, c.mode.ordered ? 1 : 0);
        wU8(o, (int)c.mode.modes.size());
        for (size_t i = 0; i < c.mode.modes.size(); ++i) {
            wStr(o, c.mode.modes[i].value);
            wU8(o, (int)c.mode.modes[i].resources.size());
            for (size_t j = 0; j < c.mode.modes[i].resources.size(); ++j)
                wStr(o, c.mode.modes[i].resources[j]);
        }
        wU8(o, (int)c.mode.resources.size());
        for (size_t i = 0; i < c.mode.resources.size(); ++i) wStr(o, c.mode.resources[i]);
        packSemantics(o, c.mode.hasSemantics, c.mode.semantics);
    } else if (c.code == kCapToggle) {
        wStr(o, c.instance);
        wU8(o, (int)c.toggle.resources.size());
        for (size_t i = 0; i < c.toggle.resources.size(); ++i) wStr(o, c.toggle.resources[i]);
        packSemantics(o, c.toggle.hasSemantics, c.toggle.semantics);
    }
    return o;
}

static int modeIndexOf(const Capability& c) {
    for (size_t i = 0; i < c.mode.modes.size(); ++i)
        if (c.mode.modes[i].value == c.state.mode) return (int)i;
    return 0;
}

static void packStateEntry(std::string& o, const Capability& c) {
    wU8(o, capCharToEnum(c.code));
    wU8(o, isInstanced(c.code) ? c.instanceIdx : 0xFF);
    switch (c.code) {
        case kCapPower: case kCapLock: case kCapToggle:
        case kCapContactSensor: case kCapMotionSensor:
        case kCapEventDetection: case kCapDoorbell:
            wU8(o, VAL_BOOL); wU8(o, c.state.b ? 1 : 0); break;
        case kCapBrightness: case kCapColorTemperature: case kCapPercentage: case kCapPowerLevel: case kCapInput: case kCapHumidity: case kCapChannel: case kCapRange:
            wU8(o, VAL_INT); wI16(o, c.state.i); break;
        case kCapMode:
            wU8(o, VAL_MODE_INDEX); wU8(o, modeIndexOf(c)); break;
        case kCapColor:
            wU8(o, VAL_COLOR); wI16(o, c.state.hue); wI16(o, c.state.sat); break;
        case kCapTemperatureSensor:
            wU8(o, VAL_TEMP); wI16(o, c.state.tempDeci); wU8(o, tempScaleToEnum(c.state.scale)); break;
        case kCapThermostat:
            wU8(o, VAL_THERMOSTAT); wI16(o, c.state.tempDeci);
            wU8(o, tempScaleToEnum(c.state.scale)); wU8(o, thermoModeToEnum(c.state.mode)); break;
        case kCapSecurityPanel:
            wU8(o, VAL_ARMSTATE); wU8(o, armStateToEnum(c.state.mode)); break;
        case kCapSpeaker:
            wU8(o, VAL_SPEAKER); wI16(o, c.state.i); wU8(o, c.state.b ? 1 : 0); break;
        case kCapEqualizer:
            wU8(o, VAL_EQUALIZER); wI16(o, c.state.bass); wI16(o, c.state.mid);
            wI16(o, c.state.treble); wU8(o, eqModeToEnum(c.state.mode)); break;
    }
}

static void packEndpoint(std::string& o, const Endpoint& ep) {
    wStr(o, ep.id);
    wStr(o, ep.name);
    wStr(o, ep.desc);
    wU8(o, (int)ep.cat.size());
    for (size_t i = 0; i < ep.cat.size(); ++i) wU8(o, catNameToEnum(ep.cat[i]));
    wU8(o, (int)ep.caps.size());
    for (size_t i = 0; i < ep.caps.size(); ++i) {
        const Capability& c = ep.caps[i];
        wU8(o, capCharToEnum(c.code));
        wU8(o, isInstanced(c.code) ? c.instanceIdx : 0xFF);
        std::string cfg = packCapConfig(c);
        wU16(o, (int)cfg.size());
        o += cfg;
    }
    // State entries in caps order.
    int stateful = 0;
    for (size_t i = 0; i < ep.caps.size(); ++i) if (ep.caps[i].hasState) ++stateful;
    wU8(o, stateful);
    for (size_t i = 0; i < ep.caps.size(); ++i)
        if (ep.caps[i].hasState) packStateEntry(o, ep.caps[i]);
}

std::string packEndpoints(std::vector<Endpoint>& eps) {
    assignInstanceIdx(eps);
    std::string o;
    wU8(o, (int)eps.size());
    for (size_t i = 0; i < eps.size(); ++i) packEndpoint(o, eps[i]);
    return o;
}

ManifestPages buildManifest(std::vector<Endpoint>& eps) {
    ManifestPages pages;
    std::string header;
    wU8(header, kSchemaVersion);
    wU8(header, 0);
    if (eps.empty()) {
        wU8(header, 0);
        wU8(header, 0);
        pages.page0 = ascii85Encode(header);
        return pages;
    }
    std::string blob = packEndpoints(eps);
    std::string enc = ascii85Encode(blob);
    for (size_t i = 0; i < enc.size(); i += kPageSizeLimitChars) {
        size_t n = enc.size() - i;
        if (n > kPageSizeLimitChars) n = kPageSizeLimitChars;
        pages.content.push_back(enc.substr(i, n));
    }
    wU8(header, (int)pages.content.size());
    wU8(header, (int)eps.size());
    pages.page0 = ascii85Encode(header);
    return pages;
}

// ---------------------------------------------------------------------------
// State publish payload (SPEC §1.8)
// ---------------------------------------------------------------------------

std::string packStatePayload(int idx, const Endpoint& ep) {
    std::string o;
    wU8(o, idx);
    int stateful = 0;
    for (size_t i = 0; i < ep.caps.size(); ++i) if (ep.caps[i].hasState) ++stateful;
    wU8(o, stateful);
    for (size_t i = 0; i < ep.caps.size(); ++i)
        if (ep.caps[i].hasState) packStateEntry(o, ep.caps[i]);
    return ascii85Encode(o);
}

// ---------------------------------------------------------------------------
// Control decode + validate (SPEC §1.6)
// ---------------------------------------------------------------------------

CtlCommand parseControl(const std::string& arg, const std::vector<Endpoint>& eps) {
    CtlCommand cmd;
    std::string blob;
    if (!ascii85Decode(arg, blob)) { cmd.status = CTL_ERR_PARSE; return cmd; }
    Reader r(blob);
    int idx = r.u8();
    int en = r.u8();
    int instByte = r.u8();
    char code = capEnumToChar(en);
    if (!r.ok || code == 0) { cmd.status = CTL_ERR_PARSE; return cmd; }
    cmd.idx = idx;
    cmd.code = code;
    cmd.instance = isInstanced(code) ? instByte : kNoInstance;

    // Read value per code.
    switch (code) {
        case kCapPower: case kCapLock: case kCapToggle: case kCapScene: case kCapTimeHold:
            cmd.boolVal = r.u8() != 0; break;
        case kCapBrightness: case kCapColorTemperature: case kCapPercentage: case kCapPowerLevel: case kCapInput: case kCapRange:
            cmd.intVal = r.i16(); break;
        case kCapColor:
            cmd.hue = r.i16(); cmd.sat = r.i16(); cmd.bri = r.i16(); break;
        case kCapMode:
            cmd.mode = r.str(); break;
        case kCapThermostat:
            cmd.sub = r.u8();
            if (cmd.sub == kThermoSubSetpoint) {
                cmd.tempDeci = r.i16();
                cmd.scale = tempScaleFromEnum(r.u8());
            } else {
                cmd.mode = thermoModeFromEnum(r.u8());
            }
            break;
        case kCapSecurityPanel:
            cmd.mode = armStateFromEnum(r.u8()); break;
        case kCapSpeaker:
            cmd.intVal = r.i16(); cmd.boolVal = r.u8() != 0; break;
        case kCapPlayback:
            cmd.intVal = r.u8(); break; // playback op enum
        case kCapStepSpeaker:
            cmd.sub = r.u8();
            if (cmd.sub == kStepVolumeSub) cmd.intVal = r.i16(); // signed steps
            else cmd.boolVal = r.u8() != 0;                       // mute
            break;
        case kCapChannel:
            cmd.sub = r.u8();
            cmd.intVal = r.i16(); // sub 0: absolute number; sub 1: signed skip count
            break;
        case kCapEqualizer:
            cmd.sub = r.u8();
            if (cmd.sub == kEqualizerSubBands) { cmd.bass = r.i16(); cmd.mid = r.i16(); cmd.treble = r.i16(); }
            else { cmd.mode = eqModeFromEnum(r.u8()); }
            break;
        default:
            cmd.status = CTL_ERR_PARSE; return cmd;
    }
    if (!r.ok) { cmd.status = CTL_ERR_PARSE; return cmd; }

    // Validate endpoint index.
    if (idx < 0 || (size_t)idx >= eps.size()) { cmd.status = CTL_ERR_BAD_INDEX; return cmd; }
    const Endpoint& ep = eps[(size_t)idx];

    // Find the capability instance.
    const Capability* cap = 0;
    bool codeSeen = false;
    for (size_t i = 0; i < ep.caps.size(); ++i) {
        const Capability& c = ep.caps[i];
        if (c.code != code) continue;
        codeSeen = true;
        if (isInstanced(code)) {
            if (c.instanceIdx == cmd.instance) { cap = &c; break; }
        } else { cap = &c; break; }
    }
    if (!cap) { cmd.status = codeSeen ? CTL_ERR_BAD_INSTANCE : CTL_ERR_BAD_CODE; return cmd; }

    // Validate value range.
    switch (code) {
        case kCapPower: case kCapLock: case kCapToggle:
            break; // u8 0/1 always valid
        case kCapBrightness: case kCapPercentage: case kCapPowerLevel:
            if (cmd.intVal < 0 || cmd.intVal > 100) { cmd.status = CTL_ERR_OUT_OF_RANGE; return cmd; }
            break;
        case kCapColorTemperature:
            if (cmd.intVal < 1000 || cmd.intVal > 10000) { cmd.status = CTL_ERR_OUT_OF_RANGE; return cmd; }
            break;
        case kCapRange:
            if (cmd.intVal < cap->range.min || cmd.intVal > cap->range.max) { cmd.status = CTL_ERR_OUT_OF_RANGE; return cmd; }
            break;
        case kCapColor:
            if (cmd.hue < 0 || cmd.hue > 360 || cmd.sat < 0 || cmd.sat > 100 || cmd.bri < 0 || cmd.bri > 100) {
                cmd.status = CTL_ERR_OUT_OF_RANGE; return cmd;
            }
            break;
        case kCapMode: {
            bool found = false;
            for (size_t i = 0; i < cap->mode.modes.size(); ++i)
                if (cap->mode.modes[i].value == cmd.mode) { found = true; break; }
            if (!found) { cmd.status = CTL_ERR_OUT_OF_RANGE; return cmd; }
            break;
        }
        case kCapThermostat:
            // setpoint accepts any temperature; mode must be a known thermostatMode.
            if (cmd.sub == kThermoSubMode && thermoModeToEnum(cmd.mode) == 0) {
                cmd.status = CTL_ERR_OUT_OF_RANGE; return cmd;
            }
            break;
        case kCapChannel:
            // ChangeChannel must be a non-negative number; SkipChannels count is any signed value.
            if (cmd.sub == kChannelSubChange && cmd.intVal < 0) { cmd.status = CTL_ERR_OUT_OF_RANGE; return cmd; }
            break;
        case kCapEqualizer:
            if (cmd.sub == kEqualizerSubBands) {
                if (cmd.bass < kEqBandMin || cmd.bass > kEqBandMax ||
                    cmd.mid  < kEqBandMin || cmd.mid  > kEqBandMax ||
                    cmd.treble < kEqBandMin || cmd.treble > kEqBandMax) { cmd.status = CTL_ERR_OUT_OF_RANGE; return cmd; }
            } else if (eqModeToEnum(cmd.mode) == 0) { // SetMode: must be a known mode
                cmd.status = CTL_ERR_OUT_OF_RANGE; return cmd;
            }
            break;
    }
    cmd.status = CTL_OK;
    return cmd;
}

// ---------------------------------------------------------------------------
// Automatica facade
// ---------------------------------------------------------------------------

static int ctlTrampoline(const std::string& arg, void* ctx) {
    return static_cast<Automatica*>(ctx)->handleControl(arg);
}

Automatica::Automatica()
    : port_(0), handler_(0), handlerCtx_(0),
      everPublished_(false), lastPublishMs_(0), begun_(false), snapshotDone_(false) {}

void Automatica::setCloudPort(CloudPort* port) { port_ = port; }

int Automatica::addEndpoint(const std::string& id, const std::string& name,
                            const std::string& desc, const std::vector<std::string>& cat) {
    Endpoint ep;
    ep.id = id; ep.name = name; ep.desc = desc; ep.cat = cat;
    endpoints_.push_back(ep);
    pub_.push_back(PubSlot());
    return (int)endpoints_.size() - 1;
}

static int nextInstanceIdx(const Endpoint& ep) {
    int n = 0;
    for (size_t i = 0; i < ep.caps.size(); ++i) if (isInstanced(ep.caps[i].code)) ++n;
    return n;
}

int Automatica::addPower(int idx) {
    Capability c; c.code = kCapPower; c.instanceIdx = kNoInstance;
    endpoints_[(size_t)idx].caps.push_back(c); return 0;
}
int Automatica::addBrightness(int idx) {
    Capability c; c.code = kCapBrightness; c.instanceIdx = kNoInstance;
    endpoints_[(size_t)idx].caps.push_back(c); return 0;
}
int Automatica::addColor(int idx) {
    Capability c; c.code = kCapColor; c.instanceIdx = kNoInstance;
    endpoints_[(size_t)idx].caps.push_back(c); return 0;
}

// --- Additional singleton builders (SPEC §1.3) ------------------------------
// Each declares one singleton capability on endpoint idx. They share addSingleton
// below; the codec (packStateEntry/parseControl) already handles every code.
int Automatica::addSingleton_(int idx, char code) {
    Capability c; c.code = code; c.instanceIdx = kNoInstance;
    endpoints_[(size_t)idx].caps.push_back(c); return 0;
}
int Automatica::addColorTemperature(int idx) { return addSingleton_(idx, kCapColorTemperature); }
int Automatica::addPercentage(int idx)       { return addSingleton_(idx, kCapPercentage); }
int Automatica::addPowerLevel(int idx)       { return addSingleton_(idx, kCapPowerLevel); }
int Automatica::addStepSpeaker(int idx)      { return addSingleton_(idx, kCapStepSpeaker); }
int Automatica::addTimeHold(int idx)         { return addSingleton_(idx, kCapTimeHold); }
int Automatica::addInput(int idx)            { return addSingleton_(idx, kCapInput); }
int Automatica::addLock(int idx)             { return addSingleton_(idx, kCapLock); }
int Automatica::addContactSensor(int idx)    { return addSingleton_(idx, kCapContactSensor); }
int Automatica::addMotionSensor(int idx)     { return addSingleton_(idx, kCapMotionSensor); }
int Automatica::addTemperatureSensor(int idx){ return addSingleton_(idx, kCapTemperatureSensor); }
int Automatica::addHumiditySensor(int idx)   { return addSingleton_(idx, kCapHumidity); }
int Automatica::addChannel(int idx)          { return addSingleton_(idx, kCapChannel); }
int Automatica::addEqualizer(int idx)        { return addSingleton_(idx, kCapEqualizer); }
int Automatica::addEventDetectionSensor(int idx) { return addSingleton_(idx, kCapEventDetection); }
int Automatica::addDoorbell(int idx)         { return addSingleton_(idx, kCapDoorbell); }
int Automatica::addCamera(int idx)           { return addSingleton_(idx, kCapCamera); }
int Automatica::addThermostat(int idx)       { return addSingleton_(idx, kCapThermostat); }
int Automatica::addScene(int idx)            { return addSingleton_(idx, kCapScene); }
int Automatica::addSecurityPanel(int idx)    { return addSingleton_(idx, kCapSecurityPanel); }
int Automatica::addSpeaker(int idx)          { return addSingleton_(idx, kCapSpeaker); }
int Automatica::addPlayback(int idx)         { return addSingleton_(idx, kCapPlayback); }
int Automatica::addRange(int idx, const std::string& instance, const RangeConfig& cfg) {
    Endpoint& ep = endpoints_[(size_t)idx];
    Capability c; c.code = kCapRange; c.instance = instance; c.range = cfg;
    c.instanceIdx = nextInstanceIdx(ep);
    ep.caps.push_back(c);
    return c.instanceIdx;
}
int Automatica::addMode(int idx, const std::string& instance, const ModeConfig& cfg) {
    Endpoint& ep = endpoints_[(size_t)idx];
    Capability c; c.code = kCapMode; c.instance = instance; c.mode = cfg;
    c.instanceIdx = nextInstanceIdx(ep);
    ep.caps.push_back(c);
    return c.instanceIdx;
}
int Automatica::addToggle(int idx, const std::string& instance, const ToggleConfig& cfg) {
    Endpoint& ep = endpoints_[(size_t)idx];
    Capability c; c.code = kCapToggle; c.instance = instance; c.toggle = cfg;
    c.instanceIdx = nextInstanceIdx(ep);
    ep.caps.push_back(c);
    return c.instanceIdx;
}

void Automatica::onControl(ControlHandler handler, void* ctx) {
    handler_ = handler; handlerCtx_ = ctx;
}

Capability* Automatica::capAt(int idx, char code, int instance) {
    if (idx < 0 || (size_t)idx >= endpoints_.size()) return 0;
    Endpoint& ep = endpoints_[(size_t)idx];
    for (size_t i = 0; i < ep.caps.size(); ++i) {
        Capability& c = ep.caps[i];
        if (c.code != code) continue;
        if (isInstanced(code)) { if (c.instanceIdx == instance) return &c; }
        else return &c;
    }
    return 0;
}

void Automatica::setInitialState(int idx, char code, int instance, const CapState& st) {
    Capability* c = capAt(idx, code, instance);
    if (!c) return;
    c->state = st;
    c->hasState = true;
}

void Automatica::rebuildManifest() { manifest_ = buildManifest(endpoints_); }

void Automatica::begin() {
    rebuildManifest();
    if (port_) {
        port_->registerVariable("automaticaManifest0", &manifest_.page0);
        for (size_t i = 0; i < manifest_.content.size(); ++i) {
            std::string nm = "automaticaManifest";
            // append (i+1) as decimal
            int v = (int)i + 1;
            char buf[12]; int n = 0;
            if (v == 0) buf[n++] = '0';
            while (v > 0) { buf[n++] = (char)('0' + v % 10); v /= 10; }
            while (n > 0) nm.push_back(buf[--n]);
            port_->registerVariable(nm, &manifest_.content[i]);
        }
        port_->registerFunction("automaticaCtl", &ctlTrampoline, this);
    }
    begun_ = true;
}

int Automatica::handleControl(const std::string& arg) {
    CtlCommand cmd = parseControl(arg, endpoints_);
    if (cmd.status != CTL_OK) return cmd.status;

    if (handler_) {
        if (!handler_(cmd, handlerCtx_)) return CTL_ERR_CALLBACK;
    }

    // Apply the validated command to our state model.
    Capability* c = capAt(cmd.idx, cmd.code, cmd.instance);
    if (c) {
        switch (cmd.code) {
            case kCapPower: case kCapLock: case kCapToggle:
                c->state.b = cmd.boolVal; break;
            case kCapBrightness: case kCapColorTemperature: case kCapPercentage: case kCapPowerLevel: case kCapInput: case kCapRange:
                c->state.i = cmd.intVal; break;
            case kCapMode:
                c->state.mode = cmd.mode; break;
            case kCapColor: {
                c->state.hue = cmd.hue; c->state.sat = cmd.sat;
                // brightness is the shared b cap (single source of truth, §1.5).
                Capability* bcap = capAt(cmd.idx, kCapBrightness, kNoInstance);
                if (bcap) { bcap->state.i = cmd.bri; bcap->hasState = true; }
                break;
            }
            case kCapThermostat:
                if (cmd.sub == kThermoSubSetpoint) { c->state.tempDeci = cmd.tempDeci; c->state.scale = cmd.scale; }
                else { c->state.mode = cmd.mode; }
                break;
            case kCapSecurityPanel:
                c->state.mode = cmd.mode; break;
            case kCapSpeaker:
                c->state.i = cmd.intVal; c->state.b = cmd.boolVal; break;
            case kCapChannel:
                // ChangeChannel sets the absolute number; SkipChannels adjusts it.
                if (cmd.sub == kChannelSubChange) c->state.i = cmd.intVal;
                else { c->state.i += cmd.intVal; if (c->state.i < 0) c->state.i = 0; }
                break;
            case kCapEqualizer:
                // SetBands sets explicit levels (mode -> custom); SetMode sets the preset
                // mode (the device applies its own band preset, left to firmware).
                if (cmd.sub == kEqualizerSubBands) {
                    c->state.bass = cmd.bass; c->state.mid = cmd.mid; c->state.treble = cmd.treble;
                    c->state.mode = "";
                } else { c->state.mode = cmd.mode; }
                break;
            // kCapPlayback / kCapStepSpeaker are momentary: callback fired; no state.
        }
        if (!isMomentary(cmd.code)) c->hasState = true;
    }
    reportState(cmd.idx);
    return CTL_OK;
}

void Automatica::reportState(int idx) {
    if (idx < 0 || (size_t)idx >= endpoints_.size()) return;
    PubSlot& slot = pub_[(size_t)idx];
    slot.pending = packStatePayload(idx, endpoints_[(size_t)idx]);
    slot.dirty = true;
}

void Automatica::flushPublishes() {
    if (!port_) return;
    unsigned long now = port_->millis();
    if (everPublished_ && (now - lastPublishMs_) < kPublishMinIntervalMs) return;
    for (size_t i = 0; i < pub_.size(); ++i) {
        PubSlot& slot = pub_[i];
        if (!slot.dirty) continue;
        if (port_->publish("automaticaState", slot.pending)) {
            slot.dirty = false;
            everPublished_ = true;
            lastPublishMs_ = now;
        }
        return;
    }
}

void Automatica::emitInitialSnapshotIfReady() {
    if (snapshotDone_ || !port_) return;
    if (!port_->connected()) return;
    for (size_t i = 0; i < endpoints_.size(); ++i) {
        pub_[i].pending = packStatePayload((int)i, endpoints_[i]);
        pub_[i].dirty = true;
    }
    snapshotDone_ = true;
}

void Automatica::loop() {
    if (!begun_) return;
    emitInitialSnapshotIfReady();
    flushPublishes();
}

size_t Automatica::endpointCount() const { return endpoints_.size(); }
const Endpoint& Automatica::endpoint(int idx) const { return endpoints_[(size_t)idx]; }
const ManifestPages& Automatica::manifest() const { return manifest_; }

}  // namespace automatica
