// hvac-thermostat.ino — automatica example
// =============================================================================
// DEVICE ARCHETYPE: a full HVAC thermostat with the "HVAC components" the
// platform layers on top of Alexa.ThermostatController. ONE endpoint carries a
// mix of SINGLETON and INSTANCED capabilities plus a read-only sensor.
//
// ALEXA CAPABILITIES EXPOSED (all on a single "hvac" endpoint)
//   'h' Alexa.ThermostatController (SINGLETON, sub-op wire — SPEC §1.6):
//        - Setpoint: cmd.sub == kThermoSubSetpoint, value in cmd.tempDeci
//          (TENTHS of a degree, e.g. 215 = 21.5°) with cmd.scale
//          ("CELSIUS" | "FAHRENHEIT" | "KELVIN").
//        - Mode: cmd.sub == kThermoSubMode, value in cmd.mode. Supported modes
//          HEAT / COOL / AUTO / OFF plus ECO (energy-saving; HVAC-components ext).
//   'm' Alexa.ModeController, instance "Thermostat.Fan" (INSTANCED, index gFan):
//        - the blower fan mode AUTO / ON / CIRCULATE. Alexa has no fan sub-
//          directive on the thermostat, so the platform models fan mode as a
//          companion ModeController instance. Value in cmd.mode.
//   't' Alexa.ToggleController, instance "Thermostat.AuxiliaryHeat" (INSTANCED,
//        index gAux):
//        - auxiliary / emergency heat on/off. Value in cmd.boolVal.
//   'e' Alexa.TemperatureSensor (SINGLETON, READ-ONLY):
//        - current ambient temperature; CapState.tempDeci (tenths) + CapState.scale.
//          Reported only, never controlled (see Gotchas / loop()).
//
// ALEXA UTTERANCES THIS ENABLES
//   "Alexa, set the thermostat to 22 degrees"  -> h, kThermoSubSetpoint, cmd.tempDeci=220
//   "Alexa, set the thermostat to eco"         -> h, kThermoSubMode, cmd.mode="ECO"
//   "Alexa, set the thermostat to heat/cool/auto/off" -> h, kThermoSubMode
//   "Alexa, set the thermostat fan to on"      -> m (gFan), cmd.mode="ON"
//   "Alexa, set the thermostat fan to circulate" -> m (gFan), cmd.mode="CIRCULATE"
//   "Alexa, turn on the auxiliary heat"        -> t (gAux), cmd.boolVal=true
//   "Alexa, what's the temperature?"           -> read-back of 'e' reported state
//
// HARDWARE WIRING ASSUMPTION
//   All apply*/read* functions are stubs. Wire them to your HVAC relays, blower
//   driver, setpoint controller, and temperature sensor. No GPIO is touched here.
//
// GOTCHAS
//   - SINGLETON vs INSTANCED dispatch:
//       * 'h' and 'e' are SINGLETONS — addressed by code with instance ==
//         kNoInstance; the int the builder returns is ignored.
//       * 'm' and 't' are INSTANCED — the callback MUST match BOTH cmd.code AND
//         cmd.instance against the saved index (gFan / gAux). Declaration order
//         assigns the wire index; addMode/addToggle return it.
//   - SUB-OP wire: ThermostatController multiplexes setpoint vs mode through
//     cmd.sub (kThermoSubSetpoint / kThermoSubMode). Always branch on cmd.sub
//     before reading tempDeci vs mode.
//   - READ-ONLY SENSOR PATTERN ('e'): temperature is REPORTED, not controlled.
//     loop() polls, edge-detects a change, writes CapState via setInitialState(),
//     then reportState(idx) coalesces a publish (latest-wins, ≤1/sec).
//   - Facade object is named `home`, not `automatica`, to avoid clashing with
//     `namespace automatica`.
//   - onControl fully-qualifies automatica::CtlCommand for the .ino-generated
//     forward prototype the preprocessor inserts above `using namespace`.
// =============================================================================
#include "automatica.h"
#include "AutomaticaCloud.h"

using namespace automatica;

Automatica home;                 // facade; never name it 'automatica' (namespace clash)
AutomaticaCloud cloud(home);     // ctor wires Particle.variable/function/publish to the facade

int gEp;                         // the single HVAC endpoint index
int gFan;   // ModeController instance index (Thermostat.Fan)  — set by addMode()
int gAux;   // ToggleController instance index (Thermostat.AuxiliaryHeat) — set by addToggle()

// --- hardware stubs: replace with your HVAC relays / blower / setpoint driver ---
static void applySetpoint(int tempDeci, const std::string& scale) { (void)tempDeci; (void)scale; } // tempDeci = tenths of a degree
static void applyMode(const std::string& mode)     { /* HEAT | COOL | AUTO | ECO | OFF */ (void)mode; }
static void applyFanMode(const std::string& mode)  { /* AUTO | ON | CIRCULATE */ (void)mode; }
static void applyAuxHeat(bool on)                  { /* engage emergency heat strips */ (void)on; }
static int  readTempDeci() { return 205; /* TODO: read sensor; tenths of a degree */ }

// Control callback. The facade has already decoded + validated the directive, so
// the typed fields for the matching code are populated (SPEC §1.6).
//
// Reads: cmd.code (capability), cmd.sub (thermostat op selector), cmd.tempDeci +
//   cmd.scale (setpoint), cmd.mode (thermostat/fan mode), cmd.instance (which
//   instanced cap), cmd.boolVal (aux-heat toggle).
// Returns true when handled; false otherwise -> Lambda returns an Alexa error.
static bool onControl(const automatica::CtlCommand& cmd, void*) {
    switch (cmd.code) {
        case kCapThermostat:                 // 'h' singleton — cmd.sub selects setpoint vs mode
            if (cmd.sub == kThermoSubSetpoint) applySetpoint(cmd.tempDeci, cmd.scale); // tenths of a degree + scale
            else                               applyMode(cmd.mode);                    // kThermoSubMode: HEAT/COOL/AUTO/ECO/OFF
            return true;
        case kCapMode:      // 'm' INSTANCED — route by instance index
            if (cmd.instance == gFan) { applyFanMode(cmd.mode); return true; }         // Thermostat.Fan: AUTO/ON/CIRCULATE
            return false;                                                              // some other mode instance we don't own
        case kCapToggle:    // 't' INSTANCED — route by instance index
            if (cmd.instance == gAux) { applyAuxHeat(cmd.boolVal); return true; }      // Thermostat.AuxiliaryHeat on/off
            return false;
    }
    return false;   // unknown/unhandled code -> Lambda returns an Alexa error
}

void setup() {
    // addEndpoint(id, friendlyName, description, displayCategories) -> Alexa idx.
    //   "hvac" must match ^[a-z0-9_-]{1,24}$ and stay stable across reboots.
    //   {"THERMOSTAT"} is the Alexa display category.
    gEp = home.addEndpoint("hvac", "Thermostat", "automatica HVAC thermostat", {"THERMOSTAT"});

    home.addThermostat(gEp);        // 'h' ThermostatController singleton (addressed by code)
    home.addTemperatureSensor(gEp); // 'e' TemperatureSensor singleton, read-only (addressed by code)

    // Companion fan mode (AUTO / ON / CIRCULATE) as a ModeController instance.
    ModeConfig fan;
    fan.ordered = false;                 // discrete states, not a stepped sequence
    fan.resources.push_back("Fan");      // friendly name for the mode instance
    // ModeOption.value is the wire value; .resources are spoken synonyms.
    { ModeOption o; o.value = "AUTO";      o.resources.push_back("Auto");      fan.modes.push_back(o); }
    { ModeOption o; o.value = "ON";        o.resources.push_back("On");        fan.modes.push_back(o); }
    { ModeOption o; o.value = "CIRCULATE"; o.resources.push_back("Circulate"); fan.modes.push_back(o); }
    // addMode -> wire instance index; saved for routing in onControl.
    gFan = home.addMode(gEp, "Thermostat.Fan", fan);

    // Companion auxiliary / emergency heat toggle as a ToggleController instance.
    ToggleConfig aux;
    aux.resources.push_back("Auxiliary Heat");   // friendly name for the toggle instance
    // addToggle -> wire instance index; saved for routing in onControl.
    gAux = home.addToggle(gEp, "Thermostat.AuxiliaryHeat", aux);

    // Initial state pushed BEFORE begin() so the first cloud snapshot is correct.
    // Setpoint 21.5°C, mode ECO, fan AUTO, aux off, current temp 20.5°C.
    // Singletons take kNoInstance; instanced caps take their saved index.
    { CapState s; s.tempDeci = 215; s.scale = "CELSIUS"; s.mode = "ECO";          // 'h': tempDeci tenths + scale + mode
      home.setInitialState(gEp, kCapThermostat,        kNoInstance, s); }
    { CapState s; s.mode = "AUTO"; home.setInitialState(gEp, kCapMode,   gFan, s); } // 'm' instance gFan: CapState.mode
    { CapState s; s.b = false;     home.setInitialState(gEp, kCapToggle, gAux, s); } // 't' instance gAux: CapState.b
    { CapState s; s.tempDeci = 205; s.scale = "CELSIUS";                          // 'e' sensor: tempDeci tenths + scale
      home.setInitialState(gEp, kCapTemperatureSensor, kNoInstance, s); }

    home.onControl(onControl); // register the directive callback
    home.begin();              // build manifest + register cloud variable/function — call once
}

void loop() {
    home.loop();   // flush debounced state publishes (≤1/sec) + emit the initial snapshot

    // Read-only TemperatureSensor ('e'): poll hardware, edge-detect, report on
    // change. This is the REPORT path (sensors are reported, not controlled):
    // write the new CapState, then reportState() to queue the coalesced publish.
    static int prevTempDeci = 205;
    int nowDeci = readTempDeci();
    if (nowDeci != prevTempDeci) {
        prevTempDeci = nowDeci;
        CapState s; s.tempDeci = nowDeci; s.scale = "CELSIUS";   // tenths of a degree + scale
        home.setInitialState(gEp, kCapTemperatureSensor, kNoInstance, s);
        home.reportState(gEp);   // mark dirty; loop() flushes it (latest-wins, ≤1/sec)
    }
}
