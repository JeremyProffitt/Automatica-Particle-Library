// thermostat.ino — automatica example.
//
// DEVICE ARCHETYPE: a heating/cooling thermostat that ALSO publishes a live
// current-temperature readout, modelled as ONE endpoint with two capabilities:
// a controllable ThermostatController plus a read-only TemperatureSensor.
//
// ALEXA CAPABILITIES EXPOSED (both singletons on this one endpoint):
//   'h' addThermostat        -> Alexa.ThermostatController (CONTROLLABLE; setpoint + mode).
//        Two control sub-ops arrive via cmd.sub:
//          cmd.sub == kThermoSubSetpoint(0): cmd.tempDeci = target temp in TENTHS of a
//                                            degree, cmd.scale = "CELSIUS"|"FAHRENHEIT"|"KELVIN".
//          cmd.sub == kThermoSubMode(1)    : cmd.mode = "HEAT" | "COOL" | "AUTO" | "OFF".
//        State (CapState): .tempDeci = current setpoint, .scale = unit, .mode = HVAC mode.
//   'e' addTemperatureSensor -> Alexa.TemperatureSensor (READ-ONLY; current ambient temp).
//        State (CapState): .tempDeci (tenths of a degree) + .scale.
//   displayCategory: THERMOSTAT.
//
// ALEXA UTTERANCES ENABLED:
//   "Alexa, set the thermostat to 22 degrees"   -> SetTargetTemperature (sub=setpoint)
//   "Alexa, set the thermostat to cool"         -> SetThermostatMode    (sub=mode, "COOL")
//   "Alexa, make it warmer / cooler"            -> AdjustTargetTemperature (sub=setpoint)
//   "Alexa, what's the temperature?"            -> reads the TemperatureSensor (read-only)
//
// HARDWARE WIRING ASSUMPTION: an HVAC relay/setpoint driver (applySetpoint/applyMode)
// and a temperature probe (readTempDeci). No GPIO is touched in this skeleton.
//
// GOTCHAS:
//   * TWO CAPABILITIES, ONE ENDPOINT: the ThermostatController is controllable while
//     the TemperatureSensor is read-only. The setpoint (thermostat) and the measured
//     temperature (sensor) are SEPARATE values — don't conflate them.
//   * Both are SINGLETONS: each is addressed by its code (kCapThermostat /
//     kCapTemperatureSensor) with instance == kNoInstance, not by an instance index.
//   * SUB-OPS: ThermostatController multiplexes two directives onto one capability via
//     cmd.sub; branch on kThermoSubSetpoint vs kThermoSubMode (default here = mode).
//   * READ-ONLY SENSOR is reported via setInitialState()+reportState(), never controlled.
//   * .ino PROTOTYPE RULE: the Arduino preprocessor inserts onControl's auto-prototype
//     above `using namespace automatica;`, so the parameter is fully qualified as
//     automatica::CtlCommand here.
#include "automatica.h"
#include "AutomaticaCloud.h"

using namespace automatica;

Automatica home;                 // the core facade. Never name it 'automatica' (clashes with the namespace).
AutomaticaCloud cloud(home);     // ctor wires the Particle cloud surface (variable/function/publish).

int gThermostatEp;               // remembered endpoint index (== Alexa idx); used to route reportState().

// --- hardware stubs: replace with your HVAC relay / setpoint driver / temp probe ---
// applySetpoint: drive the target temperature (tenths of a degree) into your controller.
static void applySetpoint(int tempDeci, const std::string& scale) { (void)tempDeci; (void)scale; }
// applyMode: switch HVAC mode. Valid Alexa values: HEAT | COOL | AUTO | OFF.
static void applyMode(const std::string& mode) { /* HEAT | COOL | AUTO | OFF */ (void)mode; }
// Read the current measured temperature in tenths of a degree (CELSIUS here).
static int readTempDeci() { return 205; /* TODO: read sensor */ }

// Control callback. Signature: bool(const CtlCommand&, void* ctx); return true =
// "handled". Dispatch on cmd.code (the capability), then on cmd.sub for thermostat
// sub-ops. Singletons arrive with cmd.instance == kNoInstance (not inspected here
// because each code is unique on this endpoint).
// Fully-qualify CtlCommand (the .ino preprocessor emits the prototype before 'using namespace').
static bool onControl(const automatica::CtlCommand& cmd, void*) {
    switch (cmd.code) {
        case kCapThermostat:
            // ThermostatController carries TWO directives multiplexed on cmd.sub:
            if (cmd.sub == kThermoSubSetpoint) { applySetpoint(cmd.tempDeci, cmd.scale); return true; } // SetTargetTemperature
            else                               { applyMode(cmd.mode);                    return true; } // SetThermostatMode
        // (The read-only TemperatureSensor 'e' never produces a control directive,
        //  so there is no case for kCapTemperatureSensor here.)
    }
    return false;   // unknown/unhandled -> Lambda returns an Alexa error
}

void setup() {
    // addEndpoint(id, friendlyName, description, {displayCategories}) -> endpoint idx.
    //   id "thermostat" : stable device id (^[a-z0-9_-]{1,24}$); must persist across reboots.
    //   category THERMOSTAT drives the Alexa app's tile/icon.
    gThermostatEp = home.addEndpoint("thermostat", "Thermostat", "automatica thermostat", {"THERMOSTAT"});

    home.addThermostat(gThermostatEp);        // 'h' controllable: setpoint + mode (singleton, returns 0/ignored).
    home.addTemperatureSensor(gThermostatEp); // 'e' read-only current temperature (singleton, returns 0/ignored).

    // Initial state: setpoint 21.5C, heating, current temperature 20.5C.
    // Thermostat singleton -> kCapThermostat + kNoInstance. .tempDeci is the SETPOINT here.
    { CapState s; s.tempDeci = 215; s.scale = "CELSIUS"; s.mode = "HEAT";
      home.setInitialState(gThermostatEp, kCapThermostat,       kNoInstance, s); }
    // Sensor singleton -> kCapTemperatureSensor + kNoInstance. .tempDeci is the MEASURED temp.
    { CapState s; s.tempDeci = 205; s.scale = "CELSIUS";
      home.setInitialState(gThermostatEp, kCapTemperatureSensor, kNoInstance, s); }

    home.onControl(onControl);   // register the control callback.
    home.begin();                // build manifest + register cloud var/function. Call once, after all add*().
}

void loop() {
    home.loop();                 // REQUIRED every loop: flush debounced publishes + emit the initial snapshot.

    // Read-only TemperatureSensor: poll hardware, edge-detect, and report on change.
    // (We do NOT report the thermostat here — its state changes only via control directives.)
    static int prevTempDeci = 205;
    int nowDeci = readTempDeci();
    if (nowDeci != prevTempDeci) {
        prevTempDeci = nowDeci;
        // Update the sensor capability's stored state, then mark it dirty so the core
        // publishes the new reading (coalesced, <=1/sec).
        CapState s; s.tempDeci = nowDeci; s.scale = "CELSIUS";
        home.setInitialState(gThermostatEp, kCapTemperatureSensor, kNoInstance, s);
        home.reportState(gThermostatEp);
    }
}
