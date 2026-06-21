// crash-report.ino — automatica example: a Particle device that detects an
// unexpected reset (panic / watchdog / pin-reset / unknown) and publishes a
// compact crash record via Particle.publish() on the next cloud-connected boot,
// enabling fleet crash-free-rate tracking.
//
// DEVICE ARCHETYPE: a smart-plug node (Photon/Argon/Boron) that:
//   1. Controls a single Alexa-addressable outlet (on/off).
//   2. Detects crash resets using System.resetReason() and persists boot + crash
//      counters across reboots in SRAM retained memory.
//   3. On the first cloud-connected loop after a crash boot, publishes one
//      "automatica/crash" event (PRIVATE) so the fleet's crash-free rate can
//      be tracked server-side (Particle webhook -> backend).
//   4. Demonstrates a deliberate watchdog timeout trigger from a serial command
//      so you can confirm the crash-report pipeline end-to-end.
//
// ALEXA CAPABILITIES EXPOSED:
//   'o' Alexa.PowerController (on/off) — smart-plug outlet
//
// ALEXA UTTERANCES ENABLED:
//   "Alexa, turn on the desk outlet."
//   "Alexa, turn off the desk outlet."
//
// CRASH TOPIC (analogous to SPEC §11.1 — published as a Particle event):
//   Event name: "automatica/crash"  (PRIVATE, NOT retained)
//   Published ONCE per crash boot, on the first Particle.connected() rising-edge.
//   NOT published on clean power-on, OTA update, System.reset(), or safe-mode.
//
// CRASH PAYLOAD SCHEMA (analogous to SPEC §11.2) — JSON string in the event data:
//   {
//     "reason":  "<resetReason>",  // e.g. "panic", "watchdog", "pin-reset", "unknown"
//     "data":    <uint32>,         // System.resetReasonData() — fault address, etc.
//     "boot":    <uint>,           // cumulative boot count (all resets)
//     "crashes": <uint>            // cumulative crash count (crash resets only)
//   }
//
// WHICH RESET REASONS COUNT AS A CRASH (published):
//   RESET_REASON_PANIC          — firmware panic / hardfault                  -> "panic"
//   RESET_REASON_WATCHDOG       — application or system watchdog timeout       -> "watchdog"
//   RESET_REASON_UPDATE_TIMEOUT — OTA update watchdog timed out (stall)       -> "update-timeout"
//   RESET_REASON_POWER_BROWNOUT — supply-voltage brownout (mirrors ESP32)     -> "brownout"
//   RESET_REASON_PIN_RESET      — external reset pin (conservative)            -> "pin-reset"
//   RESET_REASON_UNKNOWN        — unknown cause (conservative)                 -> "unknown"
//
// NOT A CRASH (no publish):
//   RESET_REASON_NONE            — normal power-on / cold start
//   RESET_REASON_POWER_DOWN      — clean power cycle
//   RESET_REASON_POWER_MANAGEMENT— power-management (Gen3 soft power)
//   RESET_REASON_USER            — System.reset() / user button
//   RESET_REASON_UPDATE          — OTA firmware update (clean)
//   RESET_REASON_SAFE_MODE       — safe mode entry
//   RESET_REASON_DFU_MODE        — DFU / USB flashing
//   RESET_REASON_FACTORY_RESET   — factory reset
//   RESET_REASON_SLEEP_TIMER     — deep-sleep timer wake (scheduled, not a crash)
//
// PERSISTENCE (retained memory):
//   Particle Device-OS provides `retained` variables backed by battery-backed SRAM
//   (Gen2: hardware retained SRAM; Gen3/Gen4: emulated via flash).  They survive
//   software resets, watchdog resets, and panic resets, but are cleared on cold
//   power-on.  This is the idiomatic Particle analog to ESP32 NVS for counters.
//   A "magic" cookie value guards against a cold-start false-positive (the retained
//   area is zeroed on power-on, so the cookie being wrong means first-ever boot).
//
//   Keys stored:
//     gBootCount  — incremented on every boot (uint32_t retained)
//     gCrashCount — incremented only on crash boots (uint32_t retained)
//     gCrashPending — true if a crash was detected this boot but not yet published
//     gMagic      — sentinel to validate the retained area after a cold power-on
//
// REPORT-ONCE SEMANTICS:
//   isCrashBoot() (constructor-equivalent) sets gCrashPending=true and increments
//   gCrashCount on a crash boot.  loop() publishes once on the first
//   Particle.connected() rising-edge when gCrashPending==true, then clears it.
//   Subsequent clean reboots do not re-report.
//
// HOW TO TRIGGER A TEST WATCHDOG:
//   Send the string "CRASH\n" over the serial port (115200 baud).  The sketch
//   starts an ApplicationWatchdog with a 3 s timeout, then enters an infinite
//   busy-loop without kicking it.  After 3 s the watchdog fires
//   (RESET_REASON_WATCHDOG).  On the next boot, a crash record is published as
//   a Particle event "automatica/crash".  Subscribe in the Particle Console
//   (Events tab, filter "automatica/crash") or via a webhook to see it.
//
// CONSTRUCTION ORDER (important — mirrors ESP32 EspVitalsSource / EspCrashSource):
//   Global setup (before setup()) validates retained area, increments gBootCount,
//   checks reset reason, increments gCrashCount and sets gCrashPending if needed.
//   All of this happens in initCrashState() called at the TOP of setup(), before
//   any other initialisation that might publish.
//
// .INO PREPROCESSOR GOTCHA:
//   The Arduino/.ino preprocessor auto-generates function prototypes and places
//   them ABOVE the first #include.  Any free function whose signature mentions a
//   type from an included header (e.g. automatica::CtlCommand) must use the fully-
//   qualified name so the generated prototype compiles.  Here onControl() uses
//   `const automatica::CtlCommand&` for exactly this reason.
//
// FLASH / RAM:
//   retained area: 5 x 4 bytes = 20 B (five uint32_t in battery-backed SRAM).
//   crash JSON payload: ~120 B temporary String (freed after Particle.publish).
//   Total overhead vs a plain smart-plug sketch: negligible.
//
// SPEC §11 (analogous).
#include "automatica.h"
#include "AutomaticaCloud.h"

using namespace automatica;

// ============================================================================
// Retained-memory crash state
// A "magic" cookie distinguishes a cold-start (no valid data) from a warm
// reboot (data valid). Value chosen to be unlikely to appear in uninitialized RAM.
// ============================================================================
static const uint32_t kRetainedMagic = 0xA7C2B4E1UL;

retained uint32_t gMagic;       // kRetainedMagic when the area has been initialised
retained uint32_t gBootCount;   // cumulative boot counter (all resets)
retained uint32_t gCrashCount;  // cumulative crash counter (crash resets only)
retained uint32_t gCrashReason; // System.resetReason() value saved on a crash boot
retained uint32_t gCrashData;   // System.resetReasonData() saved on a crash boot

// Set in initCrashState(); read in loop() to drive the one-shot publish.
static bool       gCrashPending = false;

// ---- core + Particle cloud adapter ------------------------------------------
Automatica home;               // the facade; never name it 'automatica' (namespace clash)
AutomaticaCloud cloud(home);   // Particle Device-OS adapter; ctor wires Particle.variable/function

// ---- device state -----------------------------------------------------------
static int  gPlugIdx = -1;
static bool gOn      = false;

// ============================================================================
// resetReasonString() — map Particle reset reason enum to a short string.
// System.resetReason() returns one of the RESET_REASON_* values defined in
// system_defs.h (Device-OS).  All known crash-class reasons are enumerated;
// the remainder are "clean" (no publish).
// ============================================================================
static const char* resetReasonString(int reason) {
    switch (reason) {
        case RESET_REASON_PANIC:            return "panic";
        case RESET_REASON_WATCHDOG:         return "watchdog";
        case RESET_REASON_UPDATE_TIMEOUT:   return "update-timeout";
        case RESET_REASON_POWER_BROWNOUT:   return "brownout";
        case RESET_REASON_PIN_RESET:        return "pin-reset";
        case RESET_REASON_UNKNOWN:          return "unknown";
        // Non-crash reasons:
        case RESET_REASON_NONE:             return "power-on";
        case RESET_REASON_POWER_DOWN:       return "power-down";
        case RESET_REASON_POWER_MANAGEMENT: return "power-mgmt";
        case RESET_REASON_USER:             return "user";
        case RESET_REASON_UPDATE:           return "ota-update";
        case RESET_REASON_SAFE_MODE:        return "safe-mode";
        case RESET_REASON_DFU_MODE:         return "dfu";
        case RESET_REASON_FACTORY_RESET:    return "factory-reset";
        default:                            return "unknown";
    }
}

// ============================================================================
// isCrashReason() — return true for reset causes that indicate a crash.
// Conservative: pin-reset and unknown are included (they can mask a real crash).
// ============================================================================
static bool isCrashReason(int reason) {
    switch (reason) {
        case RESET_REASON_PANIC:
        case RESET_REASON_WATCHDOG:
        case RESET_REASON_UPDATE_TIMEOUT:
        case RESET_REASON_POWER_BROWNOUT:   // supply-voltage brownout = unintentional reset
        case RESET_REASON_PIN_RESET:        // external reset pin (conservative)
        case RESET_REASON_UNKNOWN:          // unknown cause (conservative)
            return true;
        default:
            return false;
    }
}

// ============================================================================
// initCrashState() — call at the very top of setup(), before any cloud ops.
//
// 1. Validates the retained area (initialises counters on first cold boot).
// 2. Increments gBootCount unconditionally.
// 3. If the reset reason is a crash, increments gCrashCount, saves the reason
//    and fault data, and sets gCrashPending=true so loop() publishes once.
// ============================================================================
static void initCrashState() {
    if (gMagic != kRetainedMagic) {
        // Cold start or first boot: retained area is uninitialised — zero everything.
        gBootCount   = 0;
        gCrashCount  = 0;
        gCrashReason = RESET_REASON_NONE;
        gCrashData   = 0;
        gMagic       = kRetainedMagic;
    }

    // Increment boot counter on every reset (crash or clean).
    gBootCount++;

    // Check reset reason and arm pending crash publish if needed.
    int reason = System.resetReason();
    if (isCrashReason(reason)) {
        gCrashCount++;
        gCrashReason   = (uint32_t)reason;
        gCrashData     = (uint32_t)System.resetReasonData();
        gCrashPending  = true;
    } else {
        gCrashPending  = false;
    }
}

// ============================================================================
// publishCrashRecord() — build and publish the crash JSON payload once.
// Called from loop() on the first connected rising-edge after a crash boot.
//
// Publishes to event name "automatica/crash" (PRIVATE).
// Set up a Particle webhook on this event name to forward to your backend.
// Payload shape:
//   {"reason":"panic","data":0,"boot":4,"crashes":2}
// ============================================================================
static void publishCrashRecord() {
    // Snapshot counters before clearing pending flag.
    uint32_t boot      = gBootCount;
    uint32_t crashes   = gCrashCount;
    uint32_t reasonVal = gCrashReason;
    uint32_t dataVal   = gCrashData;

    // Clear the pending flag BEFORE publishing so that a publish failure does
    // not cause repeated retries (accept at-most-once semantics for crash records
    // rather than risk a crash-publish storm on a flaky connection).
    gCrashPending = false;

    // Build compact JSON payload.
    String payload = String::format(
        "{\"reason\":\"%s\",\"data\":%lu,\"boot\":%lu,\"crashes\":%lu}",
        resetReasonString((int)reasonVal),
        (unsigned long)dataVal,
        (unsigned long)boot,
        (unsigned long)crashes
    );

    // PRIVATE publish — goes only to this device owner's event stream.
    // Wire a Particle webhook on "automatica/crash" to forward to your backend.
    bool ok = Particle.publish("automatica/crash", payload, PRIVATE);
    Serial.printlnf("Crash record published (ok=%d): %s", ok ? 1 : 0, payload.c_str());
}

// ============================================================================
// Control callback
// Invoked by the library when an Alexa directive arrives (decoded automaticaCtl).
// Fully-qualified CtlCommand — required by the .ino auto-prototype rule.
// ============================================================================
static bool onControl(const automatica::CtlCommand& cmd, void* /*ctx*/) {
    if (cmd.idx == gPlugIdx && cmd.code == kCapPower) {
        gOn = cmd.boolVal;
        digitalWrite(D7, gOn ? HIGH : LOW);
        return true;
    }
    return false;
}

// ============================================================================
// setup()
// ============================================================================
void setup() {
    Serial.begin(115200);

    // MUST be first: validates retained area, increments boot count, detects crash.
    initCrashState();

    // Relay / LED output (D7 = Photon/Argon on-board LED, handy for wiring-free demo).
    pinMode(D7, OUTPUT);
    digitalWrite(D7, gOn ? HIGH : LOW);

    // ---- Endpoints ----------------------------------------------------------
    gPlugIdx = home.addEndpoint("desk-outlet", "Desk Outlet",
                                "automatica crash-report plug", {"SMARTPLUG"});
    home.addPower(gPlugIdx);

    { CapState s; s.b = gOn; home.setInitialState(gPlugIdx, kCapPower, kNoInstance, s); }

    home.onControl(onControl);
    home.begin();   // build manifest + register Particle.variable/function; call once

    // Print boot diagnostics to the serial console.
    Serial.printlnf("Reset reason:  %s (%d)",
                    resetReasonString(System.resetReason()), System.resetReason());
    Serial.printlnf("Reset data:    0x%08lX", (unsigned long)System.resetReasonData());
    Serial.printlnf("Boot count:    %lu", (unsigned long)gBootCount);
    Serial.printlnf("Crash count:   %lu", (unsigned long)gCrashCount);
    Serial.printlnf("Crash pending: %s", gCrashPending ? "YES" : "no");
}

// ============================================================================
// loop()
// ============================================================================

// Tracks the previous Particle.connected() state so we detect the rising edge.
static bool sPrevConnected = false;

// ApplicationWatchdog instance used only during test-panic; kept at file scope
// so its destructor does not fire (we never want to un-arm it once armed for test).
static ApplicationWatchdog* sTestWatchdog = nullptr;

void loop() {
    home.loop();   // REQUIRED: flush debounced state publishes + emit initial snapshot

    // ---- Crash record: publish on the first connect rising-edge after crash boot ---
    // We wait for a connect rising-edge (not just connected()) so we don't retry on
    // every already-connected loop iteration, and so the publish lands after the
    // cloud link is freshly confirmed.
    bool connected = Particle.connected();
    if (connected && !sPrevConnected && gCrashPending) {
        publishCrashRecord();
    }
    sPrevConnected = connected;

    // ---- Test-panic hook: send "CRASH\n" over serial to trigger a watchdog reset ----
    // Starts a 3-second ApplicationWatchdog and then busy-loops without kicking it.
    // After 3 s the watchdog fires (RESET_REASON_WATCHDOG).  On the next boot the
    // crash record is published to "automatica/crash".
    // Subscribe in the Particle Console (Events tab, filter "automatica/crash").
    if (Serial.available()) {
        String line = Serial.readStringUntil('\n');
        line.trim();
        if (line == "CRASH") {
            Serial.println("Triggering test watchdog timeout in 3 seconds...");
            Serial.flush();
            // Arm a 3-second watchdog (fires System.reset() with watchdog reason).
            // The lambda (or static trampoline on older toolchains) just does nothing
            // — the watchdog fires because we never call it back within the timeout.
            sTestWatchdog = new ApplicationWatchdog(3000U, std::function<void(void)>([](){
                // Watchdog handler: called when the timeout expires.
                // Returning without resetting lets Device-OS perform the reset.
            }));
            // Busy-loop to starve the watchdog — never kick it.
            while (true) { /* intentional stall */ }
        }
    }
}
