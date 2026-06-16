#pragma once
// =============================================================================
// LightSettings — unified, reusable persisted-settings store for automatica light
// firmwares (SPEC: device-side convenience, not part of the wire contract).
//
// WHY: a SINGLE owner of all persisted light state, so independent features never
// clobber each other's settings with their own scattered EEPROM writes. Add a
// field here and every firmware persists it consistently.
//
// BACKENDS (selected at compile time):
//   • ESP32      -> NVS via <Preferences.h>   (the ESP32's "EEPROM")
//   • Particle   -> emulated EEPROM (EEPROM.put / EEPROM.get)
//   • host/other -> no-op (settings stay at their in-RAM defaults; keeps the
//                   Device-OS-free host build + Catch2 tests compiling)
//
// TOGGLE: begin(enabled) gates ALL load/save from one place — wire `enabled` to a
// single bool constant at the top of your sketch to turn persistence on/off.
//
// EXTENDING WITHOUT INTERFERING WITH EXISTING DATA:
//   1. add a field to struct LightState (give it a default)
//   2. bump LIGHT_SETTINGS_VERSION
// On a magic/version mismatch the stored blob is ignored and defaults are used, so
// a firmware update never misreads old data into a newly added field.
// =============================================================================
#include <stdint.h>

#if defined(ESP32)
  #include <Preferences.h>
#endif

namespace automatica {

// Bump LIGHT_SETTINGS_VERSION whenever the LightState layout changes.
static const uint32_t LIGHT_SETTINGS_MAGIC   = 0x4C474854UL; // 'LGHT'
static const uint16_t LIGHT_SETTINGS_VERSION = 1;

// All persisted light settings live here — ONE struct, ONE owner.
// Units/ranges match the automatica capability model (SPEC §1.3):
struct LightState {
    uint32_t magic     = LIGHT_SETTINGS_MAGIC;
    uint16_t version   = LIGHT_SETTINGS_VERSION;
    uint8_t  power     = 0;     // PowerController: 0=off, 1=on
    uint8_t  mode      = 0;     // app render mode (e.g. 0=solid, 1=rainbow)
    int16_t  bri       = 100;   // BrightnessController: 0..100 (percent)
    int16_t  hue       = 0;     // ColorController hue: 0..360 (degrees)
    int16_t  sat       = 0;     // ColorController saturation: 0..100 (percent)
    int16_t  colorTemp = 4000;  // ColorTemperatureController: Kelvin (tunable white)
    // --- add new persisted settings below; then bump LIGHT_SETTINGS_VERSION ---
};

class LightSettings {
public:
    LightState state;   // live settings; equals the defaults above until load() succeeds

    explicit LightSettings(const char* ns = "light") : ns_(ns), enabled_(true) {}

    // Wire `enabled` to the single top-of-sketch persistence toggle.
    void begin(bool enabled) {
        enabled_ = enabled;
#if defined(ESP32)
        // Pre-create the NVS namespace so the first read-only load() doesn't log a
        // spurious nvs_open NOT_FOUND error before anything has been saved.
        if (enabled_) { Preferences p; if (p.begin(ns_, false)) p.end(); }
#endif
    }
    bool enabled() const { return enabled_; }

    // Load persisted settings into `state`. Returns true if valid stored data was
    // applied; false (defaults kept) when disabled / absent / version mismatch.
    bool load() {
        if (!enabled_) return false;
        LightState tmp;
        if (readBlob(tmp) && tmp.magic == LIGHT_SETTINGS_MAGIC && tmp.version == LIGHT_SETTINGS_VERSION) {
            state = tmp;
            return true;
        }
        return false;
    }

    // Persist `state` (no-op if disabled). Caller should coalesce frequent saves
    // (e.g. flush ~1.5 s after the last change) to spare flash wear.
    void save() {
        if (!enabled_) return;
        state.magic   = LIGHT_SETTINGS_MAGIC;
        state.version = LIGHT_SETTINGS_VERSION;
        writeBlob(state);
    }

private:
    const char* ns_;
    bool        enabled_;

    bool readBlob(LightState& out) {
#if defined(ESP32)
        Preferences p;
        if (!p.begin(ns_, true)) return false;
        // isKey() avoids a spurious NOT_FOUND error log before the first save.
        bool ok = p.isKey("blob")
                  && p.getBytesLength("blob") == sizeof(out)
                  && p.getBytes("blob", &out, sizeof(out)) == sizeof(out);
        p.end();
        return ok;
#elif defined(PARTICLE) || defined(SPARK)
        EEPROM.get(0, out);   // uninitialised EEPROM reads 0xFF -> magic mismatch -> defaults
        return true;
#else
        (void)out; return false;
#endif
    }

    void writeBlob(const LightState& in) {
#if defined(ESP32)
        Preferences p;
        if (!p.begin(ns_, false)) return;
        p.putBytes("blob", &in, sizeof(in));
        p.end();
#elif defined(PARTICLE) || defined(SPARK)
        EEPROM.put(0, in);    // Particle EEPROM.put only rewrites changed bytes
#else
        (void)in;
#endif
    }
};

} // namespace automatica
