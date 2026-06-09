// temperature-sensor.ino — automatica example.
//
// DEVICE ARCHETYPE: a READ-ONLY ambient temperature sensor (e.g. a room probe),
// modelled as ONE endpoint that reports the current temperature in Celsius.
//
// ALEXA CAPABILITIES EXPOSED (one singleton capability on this endpoint):
//   'e' addTemperatureSensor  -> Alexa.TemperatureSensor  (READ-ONLY; not controllable)
//        State carried in CapState:
//          .tempDeci = temperature in TENTHS of a degree (e.g. 215 -> 21.5 deg).
//          .scale    = "CELSIUS" | "FAHRENHEIT" | "KELVIN" (this example uses CELSIUS).
//        displayCategory: TEMPERATURE_SENSOR.
//
// ALEXA UTTERANCES ENABLED:
//   "Alexa, what's the temperature in the office?"
//   (No control utterances — a sensor cannot be commanded; see Gotchas.)
//
// HARDWARE WIRING ASSUMPTION: a temperature probe (DS18B20 / DHT22 / TMP36 / etc.)
// read by readTempDeci(). No GPIO is touched in this skeleton; wire your driver in.
//
// GOTCHAS:
//   * READ-ONLY SENSOR: Alexa.TemperatureSensor is reported, never controlled. We
//     push values up with setInitialState()+reportState(); the onControl handler
//     returns false for every directive (nothing here is commandable).
//   * SINGLETON capability: there is exactly one TemperatureSensor on this endpoint,
//     so it is addressed by its code (kCapTemperatureSensor) with instance ==
//     kNoInstance — never by an instance index. addTemperatureSensor() returns 0,
//     which is ignored.
//   * .ino PROTOTYPE RULE: the Arduino preprocessor auto-generates a forward
//     prototype for onControl and inserts it ABOVE the `using namespace automatica;`
//     line, so the parameter type is fully qualified as automatica::CtlCommand here.
//   * reportState() is rate-limited by the core to <=1 publish/sec (kPublishMinIntervalMs),
//     latest-wins/coalesced — so calling it too often is safe but redundant.
#include "automatica.h"
#include "AutomaticaCloud.h"

using namespace automatica;

Automatica home;                 // the core facade. Never name it 'automatica' (clashes with the namespace).
AutomaticaCloud cloud(home);     // ctor calls home.setCloudPort(this) — wires Particle.variable/.function/.publish.

int gTempEp;                     // remembered endpoint index (== Alexa endpoint idx); used to route reportState().

// --- hardware stub: read the temperature in TENTHS of a degree Celsius ---
// e.g. a return of 215 means 21.5 degC. Replace with your DS18B20/DHT/etc. driver.
// (This skeleton returns 0; the loop's change-detection therefore never fires.)
static int readTempDeci() { return 0; }

// Control callback. Signature: bool(const CtlCommand&, void* ctx); return true =
// "handled", false = "not handled" (the Lambda then returns an Alexa error).
// A pure sensor has NOTHING to control, so we reject every directive unconditionally.
// (We never inspect cmd.code/cmd.instance here because no inbound directive is valid.)
static bool onControl(const automatica::CtlCommand& cmd, void*) {
    (void)cmd;
    return false;   // read-only sensor -> Lambda returns an Alexa error for any directive
}

void setup() {
    // addEndpoint(id, friendlyName, description, {displayCategories}) -> endpoint idx.
    //   id "office"  : stable device id, ^[a-z0-9_-]{1,24}$ — must NOT change across reboots.
    //   name         : Alexa friendlyName the user speaks ("the office").
    //   category     : TEMPERATURE_SENSOR drives the Alexa app's icon/grouping.
    gTempEp = home.addEndpoint("office", "Office Temperature", "automatica temperature sensor", {"TEMPERATURE_SENSOR"});
    // Declare the single read-only Alexa.TemperatureSensor capability on this endpoint.
    // Singleton: returns 0 (ignored); later addressed by kCapTemperatureSensor + kNoInstance.
    home.addTemperatureSensor(gTempEp);

    // Seed the initial reported state from the current hardware reading so the very
    // first discovery/snapshot already carries a real value.
    //   s.tempDeci = tenths of a degree; s.scale = the unit string Alexa reports.
    // setInitialState(idx, code, instance, state): instance == kNoInstance for this singleton.
    { CapState s; s.tempDeci = readTempDeci(); s.scale = "CELSIUS"; home.setInitialState(gTempEp, kCapTemperatureSensor, kNoInstance, s); }

    home.onControl(onControl);   // register the (reject-everything) control callback.
    home.begin();                // build the manifest + register cloud var/function. Call once, after all add*().
}

void loop() {
    home.loop();                 // REQUIRED every loop: flushes debounced publishes + emits the initial snapshot.

    // Poll the temperature; report only when it changes by >= 5 tenths (0.5 degC)
    // to avoid spamming the cloud with sensor jitter.
    static int prevTempDeci = readTempDeci();
    int tempDeci = readTempDeci();
    int delta = tempDeci - prevTempDeci;
    if (delta < 0) delta = -delta;
    if (delta >= 5) {
        prevTempDeci = tempDeci;
        // Update the capability's stored state, then mark it dirty. reportState()
        // schedules a coalesced, <=1/sec publish of this endpoint's new state to the cloud.
        { CapState s; s.tempDeci = tempDeci; s.scale = "CELSIUS"; home.setInitialState(gTempEp, kCapTemperatureSensor, kNoInstance, s); }
        home.reportState(gTempEp);
    }
}
