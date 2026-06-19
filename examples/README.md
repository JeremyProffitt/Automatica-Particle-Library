# Particle examples

This directory holds the runnable firmware examples for the **automatica** Particle library.
Each subfolder is one self-contained sketch (`<name>/<name>.ino`) for a single device
archetype. Every sketch begins with a header comment that names the archetype, lists the
exact Alexa capabilities it exposes (capability code + Alexa interface + singleton/instanced),
the precise utterances it enables, the hardware-wiring assumptions, and the gotchas.

There are **32 examples**. They are templates: copy one, replace the `apply*`/`read*` stubs
with your hardware, flash, and your device speaks Alexa Smart Home with no JSON, HTTP, or
OAuth on the MCU.

See also:

- [`../README.md`](../README.md) — library overview, install, capability reference, gotchas.
- [`../../../docs/product/particle-library-guide.md`](../../../docs/product/particle-library-guide.md) — the LLM-facing library guide.
- [`../../../docs/product/capabilities.md`](../../../docs/product/capabilities.md) — capability catalog.
- [`../../../contract/SPEC.md`](../../../contract/SPEC.md) — the authoritative v2 wire contract (binary layout, Ascii85, enum tables, capability codes).

---

## How an example reaches Alexa

Every sketch declares endpoints + capabilities, registers one control callback, and calls
`begin()`/`loop()`. The device exposes exactly three Particle Cloud primitives; the
self-hosted automatica Lambda is the only party that ever produces Alexa JSON.

```mermaid
flowchart LR
  A["Alexa Smart Home<br/>(voice / app)"] -- JSON directive --> L["automatica Lambda<br/>(Go)"]
  L -- "Ascii85 binary<br/>(automaticaCtl function)" --> PC["Particle Cloud REST"]
  PC -- "control blob" --> D["Particle device<br/>(your sketch)"]
  D -- "onControl(cmd)" --> HW["your hardware<br/>(apply* / read*)"]
  D -- "automaticaManifest0..N (variable)" --> PC
  D -- "automaticaState (publish, &le;1/s)" --> PC
  PC -- discovery / state --> L
  L -- Alexa.Response / ChangeReport --> A
```

| Surface | Particle primitive | Purpose |
|---|---|---|
| `automaticaManifest0..N` | `Particle.variable` (string) | Paged binary discovery manifest (Ascii85). |
| `automaticaCtl` | `Particle.function` → `int` | Control command (Ascii85 binary); returns `0` or a negative `CtlStatus`. |
| `automaticaState` | `Particle.publish` (PRIVATE) | Coalesced, latest-wins state changes (Ascii85 binary), ≤1/sec. |

Generic Logger records use a separate webhook bridge example:
[`logger-ingest/`](logger-ingest/). It publishes a Particle event such as
`logger/temperature/sensor-01`; configure a Particle webhook to POST that event
to the Logger HTTP API. This is not native AWS IoT MQTT.

---

## Build & flash any example

The library registry id is lowercase `automatica`. Pick a folder, then compile and flash it
to your device. Substitute your platform (`photon`, `photon2`, `p2`, `boron`, `argon`, …)
and your device name.

### Particle CLI

```sh
# From this examples/ directory, compile one example folder for your platform:
particle compile boron ./color-light --saveTo firmware.bin

# Flash it to your device (OTA over Particle Cloud):
particle flash <device-name> firmware.bin

# Or compile + flash in one step (cloud build):
particle flash <device-name> ./color-light
```

`./color-light` compiles `color-light/color-light.ino` together with the library `src/`.
The library is resolved from `library.properties` (`name=automatica`). Examples that drive an
RGB pixel (e.g. `color-light/`) pull `dependencies.neopixel=1.0.4`; the core itself has no
external dependency.

### Particle Workbench (VS Code)

Open the repository as a Particle project, select the example `.ino`, choose your device and
platform, then run **Particle: Cloud Flash** (or **Particle: Compile**). Workbench reads
`library.properties` and treats `src/` as the library automatically.

> Endpoint declaration order is the device identity — the array index *is* the Alexa idx and
> must stay stable across reboots. Each endpoint `id` must match `^[a-z0-9_-]{1,24}$`.

---

## Examples by capability

Capability codes (`o`, `b`, `r`, …) are defined in [`../../../contract/SPEC.md`](../../../contract/SPEC.md)
and tabulated in [`../README.md`](../README.md#capability-reference). Singletons are
one-per-endpoint (addressed by `cmd.code`); instanced caps (`r`/`m`/`t`) may appear many times
per endpoint (addressed by `cmd.code` **and** `cmd.instance`).

### Lights

| Example | Capabilities | One-liner |
|---|---|---|
| [`dimmable-light/`](dimmable-light/) | `o` + `b` | On/off plus brightness (0–100). |
| [`color-light/`](color-light/) | `o` + `b` + `c` | RGB color light; color carries hue+sat, brightness stays on shared `b`. |
| [`tunable-white-light/`](tunable-white-light/) | `o` + `b` + `k` | White light with color temperature in Kelvin (1000–10000). |
| [`percentage-dimmer/`](percentage-dimmer/) | `o` + `p` | Generic PercentageController 0–100 (not a light-specific brightness). |
| [`power-level/`](power-level/) | `o` + `w` | PowerLevelController 0–100 (e.g. a heater element). |

### Climate

| Example | Capabilities | One-liner |
|---|---|---|
| [`thermostat/`](thermostat/) | `h` + `e` | Setpoint + mode (HEAT/COOL/AUTO/OFF) with a current-temperature read-back. |
| [`hvac-thermostat/`](hvac-thermostat/) | `h` + `m` + `t` + `e` | Full HVAC: thermostat with ECO, plus instanced fan mode and aux-heat toggle. |
| [`temperature-sensor/`](temperature-sensor/) | `e` | Read-only TemperatureSensor reporting via `reportState()`. |
| [`humidity-sensor/`](humidity-sensor/) | `n` | Read-only humidity as a RangeController percent (0–100). |
| [`sensor-fusion/`](sensor-fusion/) | `e` + `n` | Two-channel environmental node using `SensorPipeline` (calibration, EMA smoothing, deadband, Ledger config). |

### Media & audio

| Example | Capabilities | One-liner |
|---|---|---|
| [`media-player/`](media-player/) | `y` + `u` | Transport ops (play/pause/stop/next/…) plus volume + mute. |
| [`speaker/`](speaker/) | `u` | Absolute volume (0–100) + mute. |
| [`step-speaker/`](step-speaker/) | `o` + `g` | Relative, momentary volume steps + mute (e.g. an IR receiver). |
| [`channel-tv/`](channel-tv/) | `o` + `f` | ChannelController: change channel (absolute) / skip channels (signed). |
| [`av-input/`](av-input/) | `o` + `j` | InputController: select input by index (HDMI 1, HDMI 2, …). |
| [`equalizer-soundbar/`](equalizer-soundbar/) | `o` + `q` | EqualizerController: bass/mid/treble bands plus preset modes. |
| [`time-hold/`](time-hold/) | `o` + `i` | TimeHoldController: momentary Hold/Resume (e.g. pause/resume a washer). |

### Access & security

| Example | Capabilities | One-liner |
|---|---|---|
| [`door-lock/`](door-lock/) | `l` | LockController lock/unlock with state read-back. |
| [`garage-door/`](garage-door/) | `m` (+ semantics) | Garage opener as an instanced ModeController with Open/Close semantics + PIN. |
| [`security-panel/`](security-panel/) | `a` | SecurityPanelController arm/disarm (DISARMED / ARMED_AWAY / ARMED_STAY / ARMED_NIGHT). |
| [`doorbell/`](doorbell/) | `z` + `x` | Proactive DoorbellEventSource + EventDetectionSensor (fires a DoorbellPress). |
| [`camera/`](camera/) | `C` | Stateless CameraStreamController; the Lambda renders the snapshot `imageUri`. |

### Covers & motion

| Example | Capabilities | One-liner |
|---|---|---|
| [`roller-blind/`](roller-blind/) | `r` (+ semantics) | Position 0–100 with open/close/raise/lower semantics. |
| [`curtain/`](curtain/) | `r` (+ semantics) | `CURTAIN` position with open/close semantics. |
| [`ceiling-fan/`](ceiling-fan/) | `o` + `r` + `t` + `m` | Canonical instanced example: power + speed + oscillate + direction. |

### Sensors (read-only)

| Example | Capabilities | One-liner |
|---|---|---|
| [`window-sensor/`](window-sensor/) | `d` | Read-only ContactSensor (open/closed) via `reportState()`. |
| [`motion-sensor/`](motion-sensor/) | `v` | Read-only MotionSensor (detected / not detected). |

### Switches & plugs

| Example | Capabilities | One-liner |
|---|---|---|
| [`smart-switch/`](smart-switch/) | `o` | On/off wall switch (`SWITCH` display category). |
| [`smart-plug/`](smart-plug/) | `o` | On/off plug/outlet (`SMARTPLUG`/`OUTLET`). |

### Scenes & multi-device

| Example | Capabilities | One-liner |
|---|---|---|
| [`scene-controller/`](scene-controller/) | `s` | Momentary SceneController (Activate/Deactivate). |
| [`multi-endpoint/`](multi-endpoint/) | mixed | One MCU exposing several Alexa endpoints; routes directives by `cmd.idx`. |

---

## Deep dives

Three walkthroughs cover the patterns every other example reuses: a pure-singleton device,
a mixed singleton+instanced device with a read-only sensor, and an instanced device with
Alexa semantics.

### 1. `color-light/` — singleton capabilities

A NeoPixel-style RGB lamp on one endpoint (display category `LIGHT`). It demonstrates the
simplest dispatch model: all capabilities are **singletons**, addressed purely by `cmd.code`,
with `cmd.instance == kNoInstance`.

**Capabilities**

| Code | Alexa interface | Fields read in `onControl` |
|---|---|---|
| `o` | PowerController | `cmd.boolVal` (on/off) |
| `b` | BrightnessController | `cmd.intVal` 0–100 |
| `c` | ColorController | `cmd.hue` 0–360, `cmd.sat` 0–100 |

**Utterances:** "turn on the lamp", "set the lamp to blue", "set the lamp to 50%", "dim the lamp".

**Declaration (`setup`)**

```cpp
int lamp = home.addEndpoint("color_lamp", "Lamp", "automatica color light", {"LIGHT"});
home.addPower(lamp);       // 'o' — TurnOn/TurnOff
home.addBrightness(lamp);  // 'b' — level 0..100
home.addColor(lamp);       // 'c' — hue 0..360 + sat 0..100
```

**Dispatch (`onControl`)**

```cpp
static bool onControl(const automatica::CtlCommand& cmd, void*) {
    switch (cmd.code) {
        case kCapPower:      applyPower(cmd.boolVal); return true;                              // 'o'
        case kCapBrightness: gBri = cmd.intVal; setStripHsb(gHue, gSat, gBri); return true;     // 'b'
        case kCapColor:      gHue = cmd.hue; gSat = cmd.sat; setStripHsb(gHue, gSat, gBri); return true; // 'c'
    }
    return false;   // unknown/unhandled -> Lambda returns an Alexa error
}
```

**Key points**

- Brightness is the **shared `b` capability** — `SetColor` carries hue+sat only; do not
  double-own the level by reading `cmd.bri` from a color directive.
- The sketch mirrors `gHue/gSat/gBri` in globals because each directive updates only one of
  them and the strip needs all three to repaint.
- "Power off" is rendered as brightness 0 — no separate power pin is toggled.

![color-light flow](../../../docs/images/example-color-light.png)
<!-- SCREENSHOT-SLOT id=example-color-light page="Alexa app device card for the color-light example / serial monitor showing < CTL c" how="Flash color-light to a Photon, open the Particle serial monitor, say 'Alexa, set the lamp to blue', capture the serial CTL log line alongside the Alexa app color picker." -->

### 2. `hvac-thermostat/` — singleton + instanced + read-only sensor

A complete HVAC thermostat on a single endpoint (`THERMOSTAT`). It is the reference for
mixing all three dispatch styles in one callback: a sub-op singleton (`h`), two instanced
companion caps (`m`, `t`), and a read-only sensor (`e`).

**Capabilities**

| Code | Alexa interface | Kind | Fields read |
|---|---|---|---|
| `h` | ThermostatController | singleton, sub-op | `cmd.sub`; setpoint → `cmd.tempDeci` + `cmd.scale`; mode → `cmd.mode` (HEAT/COOL/AUTO/OFF/ECO) |
| `m` | ModeController `"Thermostat.Fan"` | instanced (`gFan`) | `cmd.mode` (AUTO/ON/CIRCULATE) |
| `t` | ToggleController `"Thermostat.AuxiliaryHeat"` | instanced (`gAux`) | `cmd.boolVal` |
| `e` | TemperatureSensor | singleton, read-only | reported only — `CapState.tempDeci` + `CapState.scale` |

**Utterances:** "set the thermostat to 22 degrees", "set the thermostat to eco", "set the
thermostat fan to circulate", "turn on the auxiliary heat", "what's the temperature?".

**Declaration (`setup`)**

```cpp
gEp = home.addEndpoint("hvac", "Thermostat", "automatica HVAC thermostat", {"THERMOSTAT"});
home.addThermostat(gEp);        // 'h' singleton
home.addTemperatureSensor(gEp); // 'e' singleton, read-only

ModeConfig fan; fan.ordered = false; fan.resources.push_back("Fan");
/* push AUTO / ON / CIRCULATE ModeOptions */
gFan = home.addMode(gEp, "Thermostat.Fan", fan);          // capture the wire instance index

ToggleConfig aux; aux.resources.push_back("Auxiliary Heat");
gAux = home.addToggle(gEp, "Thermostat.AuxiliaryHeat", aux); // capture the wire instance index
```

**Dispatch (`onControl`)**

```cpp
switch (cmd.code) {
    case kCapThermostat:                                       // 'h' singleton — branch on cmd.sub
        if (cmd.sub == kThermoSubSetpoint) applySetpoint(cmd.tempDeci, cmd.scale);
        else                               applyMode(cmd.mode);
        return true;
    case kCapMode:                                             // 'm' INSTANCED — match the index
        if (cmd.instance == gFan) { applyFanMode(cmd.mode); return true; }
        return false;
    case kCapToggle:                                           // 't' INSTANCED — match the index
        if (cmd.instance == gAux) { applyAuxHeat(cmd.boolVal); return true; }
        return false;
}
```

**Read-only sensor path (`loop`)**

```cpp
int nowDeci = readTempDeci();           // tenths of a degree
if (nowDeci != prevTempDeci) {
    prevTempDeci = nowDeci;
    CapState s; s.tempDeci = nowDeci; s.scale = "CELSIUS";
    home.setInitialState(gEp, kCapTemperatureSensor, kNoInstance, s);
    home.reportState(gEp);              // mark dirty; loop() flushes it (latest-wins, <=1/sec)
}
```

**Key points**

- `ThermostatController` multiplexes setpoint vs mode through `cmd.sub` — always branch on
  `cmd.sub` before reading `cmd.tempDeci` vs `cmd.mode`.
- Instanced caps (`m`, `t`) must match **both** `cmd.code` and `cmd.instance` against the saved
  index; declaration order assigns the index and `addMode`/`addToggle` return it.
- Sensors are **reported, not controlled**: poll in `loop()`, edge-detect, write `CapState`,
  then `reportState()`.

### 3. `garage-door/` — instanced ModeController with Alexa semantics

A garage opener modeled as a single instanced ModeController with two positions, plus
**semantics** that bind Alexa's built-in Open/Close verbs (and the Open/Closed state badge)
to the two mode values. The `GARAGE_DOOR` display category makes Alexa require a spoken PIN
before opening — a safety feature with no extra device code.

**Capabilities**

| Code | Alexa interface | Kind | Fields read |
|---|---|---|---|
| `m` | ModeController `"Door.Position"` | instanced (`gDoorInst`) | `cmd.mode` = `"Position.Open"` / `"Position.Closed"` |

**Utterances:** "open the garage door", "close the garage door", "set the garage door to up",
"is the garage door open?".

**Declaration (`setup`)**

```cpp
int garage = home.addEndpoint("garage", "Garage Door", "automatica garage door", {"GARAGE_DOOR"});

ModeConfig door; door.ordered = false;            // discrete states, not a stepped sequence
/* ModeOption "Position.Open"  (resources Open/Up); "Position.Closed" (resources Closed/Down) */
door.resources.push_back("Position");

door.hasSemantics = true;
// actionMappings: spoken verb -> SetMode + target value
{ ActionMapping a; a.action="Open";  a.directive="SetMode"; a.valueStr="Position.Open";   door.semantics.actionMappings.push_back(a); }
{ ActionMapping a; a.action="Close"; a.directive="SetMode"; a.valueStr="Position.Closed"; door.semantics.actionMappings.push_back(a); }
// stateMappings: kind 2 = StatesToValue(string) -> report this Alexa state when mode == value
{ StateMapping s; s.state="Open";   s.kind=2; s.valueStr="Position.Open";   door.semantics.stateMappings.push_back(s); }
{ StateMapping s; s.state="Closed"; s.kind=2; s.valueStr="Position.Closed"; door.semantics.stateMappings.push_back(s); }

gDoorInst = home.addMode(garage, "Door.Position", door);   // capture the wire instance index
{ CapState s; s.mode = "Position.Closed"; home.setInitialState(garage, kCapMode, gDoorInst, s); }
```

**Dispatch (`onControl`)**

```cpp
static bool onControl(const automatica::CtlCommand& cmd, void*) {
    if (cmd.code == kCapMode && cmd.instance == gDoorInst) {  // 'm' + match the instance
        applyDoor(cmd.mode);   // "Position.Open" or "Position.Closed"
        return true;
    }
    return false;
}
```

**Key points**

- Semantics (SPEC §1.7) let Alexa map natural Open/Close verbs and the app state badge onto
  arbitrary mode values without extra firmware.
- `GARAGE_DOOR` triggers Alexa's PIN-protected open flow — declared via the display category,
  not code.
- Even with a single mode instance, dispatch by **both** `cmd.code` and `cmd.instance`; an
  endpoint may carry several mode instances that share code `m`.

---

## Common patterns across all examples

```mermaid
flowchart TD
  C["onControl(cmd)"] --> Q{"cmd.code"}
  Q -->|singleton 'o' 'b' 'c' ...| S["dispatch by code<br/>cmd.instance == kNoInstance"]
  Q -->|instanced 'r' 'm' 't'| I["match cmd.code AND cmd.instance<br/>against the saved builder index"]
  Q -->|multi-endpoint| E["also branch on cmd.idx<br/>(which endpoint)"]
  S --> R["return true (handled) / false (Alexa error)"]
  I --> R
  E --> R
```

- **Never name the facade `automatica`** — it collides with the namespace; use `home`.
- **Construct `AutomaticaCloud cloud(home);`** at global scope right after the facade; its
  constructor calls `home.setCloudPort(this)`.
- **Fully-qualify** the callback parameter as `automatica::CtlCommand` so the `.ino`
  preprocessor's auto-generated prototype (emitted above `using namespace automatica;`) compiles.
- **Capture instanced builder return values** (`addRange`/`addMode`/`addToggle`) and compare
  against `cmd.instance`; never hard-code `0,1,2`.
- **Read-only sensors** (`d`/`v`/`e`/`n`/`x`) never accept directives — drive them via
  `reportState()` from `loop()`.
- **Momentary** caps (`s`/`y`/`g`/`i`) persist no state — act in the callback; do not call
  `setInitialState`/`reportState`.
- `begin()` is called **once** in `setup()`; `loop()` flushes coalesced publishes (≤1/sec).

For the full capability code table, `CtlCommand` field reference, and `CtlStatus` return
codes, see [`../README.md`](../README.md) and [`../../../contract/SPEC.md`](../../../contract/SPEC.md).
