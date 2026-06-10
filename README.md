<h1 align="center">automatica</h1>

<p align="center">
  <strong>Code-first Alexa Smart Home for Particle devices.</strong><br>
  Declare a voice-controlled device in ~5 lines of C++. No JSON, no HTTP, no OAuth on the device.
</p>

<p align="center">
  <img alt="library version" src="https://img.shields.io/badge/library-v0.1.1-blue">
  <img alt="platforms" src="https://img.shields.io/badge/platforms-Photon%20%7C%20P2%20%7C%20Boron%20%7C%20Argon-2aa198">
  <img alt="Alexa Smart Home" src="https://img.shields.io/badge/Alexa-Smart%20Home-00caff">
  <img alt="license" src="https://img.shields.io/badge/license-MIT-green">
</p>

---

## What is this?

`automatica` lets your Particle device speak Alexa Smart Home **without writing any cloud
code on the device**. You declare endpoints with instance-aware capabilities (power,
brightness, color, range, mode, toggle, sensors, thermostat, media, locks, cameras …) in a
few lines of C++, attach one control callback, and call `begin()`/`loop()`. A self-hosted
**automatica Lambda + Alexa skill** then discovers, controls, and queries those devices by
voice.

The on-device core is **Device-OS-free**: it owns a compact bit-packed binary wire codec
wrapped in Ascii85 (no JSON on the MCU) and is unit-tested on a desktop. The Lambda is the
only party that ever produces Alexa JSON.

```cpp
int lamp = home.addEndpoint("lamp", "Desk Lamp", "automatica desk lamp", {"LIGHT"});
home.addPower(lamp);        // "Alexa, turn on the desk lamp"
home.addBrightness(lamp);   // "Alexa, set the desk lamp to 40%"
```

> **You say:** *"Alexa, turn on the desk lamp."* &nbsp;•&nbsp; *"Set the lamp to 40%."* &nbsp;•&nbsp; *"Is the front door locked?"* &nbsp;•&nbsp; *"Set the thermostat to 22 degrees."*

---

## Features

- **26 capability builders** spanning lights, dimmers, color/tunable-white, locks,
  contact/motion/temperature/humidity sensors, thermostats, scenes, security panels,
  speakers/media transport, channels, equalizers, cameras, doorbells, and more.
- **Instance-aware** — put many ranges/modes/toggles on one endpoint (a ceiling fan with
  speed + oscillate + direction), each routed by a named instance.
- **~5-line device declaration** — no JSON, HTTP, certificates, or OAuth on the MCU.
- **Tiny, deterministic wire** — bit-packed binary + Ascii85, coalesced & rate-limited
  state publishes, paged discovery manifest.
- **Host-testable core** — Device-OS-free C++14, validated against a shared binary contract
  with Catch2.
- **Multi-platform** — Photon / Photon 2 / P2 / Boron / Argon / E-series (`architectures=*`).

---

## Install

### Particle Workbench (VS Code)
Add **automatica** from the library manager, or open this repository as a Particle project —
Workbench reads `library.properties` and treats `src/` as the library. Use *Particle:
Compile* / *Cloud Flash* on your target device.

### Particle CLI
```sh
# In a Particle project, add the dependency:
particle library add automatica

# Compile + flash a chosen platform (boron, photon2, p2, argon, …):
particle compile boron --saveTo firmware.bin
particle flash <device-name> firmware.bin
```

### `library.properties`
```properties
name=automatica
version=0.1.1
author=Jeremy Proffitt
category=IoT
architectures=*
dependencies.neopixel=1.0.4
```
`dependencies.neopixel` is pulled only by examples that drive an RGB pixel (e.g.
`color-light/`); the core does not require it.

---

## Quick start (copy-paste sketch)

```cpp
#include "automatica.h"
#include "AutomaticaCloud.h"

using namespace automatica;
Automatica home;                  // NB: do NOT name the instance `automatica`
AutomaticaCloud cloud(home);      //     (it collides with the namespace).
                                  // The adapter ctor calls home.setCloudPort(this).

// Fully-qualify the callback's CtlCommand so the Particle .ino preprocessor's
// auto-generated prototype is valid (it is emitted ABOVE the `using namespace`).
bool onControl(const automatica::CtlCommand& c, void*) {
    // c.code is the capability code ('o','b','r', …); c.instance routes instanced
    // caps. Apply to hardware; return true = handled, false = failed (Alexa error).
    return true;
}

void setup() {
    int lamp = home.addEndpoint("lamp", "Desk Lamp", "automatica desk lamp", {"LIGHT"});
    home.addPower(lamp);        // 'o' on/off
    home.addBrightness(lamp);   // 'b' 0..100
    home.onControl(onControl);
    home.begin();               // build manifest + register cloud var/function; once
}

void loop() { home.loop(); }    // flush debounced publishes + initial snapshot
```

That enables: *"Alexa, turn on the desk lamp"*, *"set the desk lamp to 40%"*,
*"dim the desk lamp"*.

---

## How it works

```
Alexa  ──JSON──▶  automatica Lambda  ──Ascii85 binary──▶  Particle Cloud
                                                              │
                          automaticaManifest0..N (variable)   │   automaticaCtl (function)
                          automaticaState (publish) ◀──────────┴────────────────▶ device
                                                       Automatica facade ──▶ onControl ──▶ your hardware
```

The device exposes exactly three cloud primitives (contract **v2**):

| Surface | Particle primitive | Purpose |
|---|---|---|
| `automaticaManifest0..N` | `Particle.variable` (string) | Paged binary discovery manifest (Ascii85). Page 0 is a 4-byte header; pages 1..N each ≤ 864 chars. |
| `automaticaCtl` | `Particle.function` → `int` | Control (Ascii85 binary command). Returns `0` on success or a negative `CtlStatus`. |
| `automaticaState` | `Particle.publish` (PRIVATE) | Coalesced state changes (Ascii85 binary), latest-wins, ≤1/sec. |

The schema version byte (`ver` in `automaticaManifest0`) is `kSchemaVersion = 2`.

---

## Capability reference

All 26 capability codes are below. **Singletons** are one-per-endpoint, addressed by *code*
(`cmd.code`, instance `kNoInstance`). **Instanced** capabilities (`r`/`m`/`t`) can appear
many times per endpoint, each with a distinct named instance, addressed by *code + wire
instance index* (`cmd.instance`, the value the `add*` builder returned).

### Singleton builders

| Code | Builder | Alexa interface | Value / `CtlCommand` field | Example | Sample utterance |
|---|---|---|---|---|---|
| `o` | `addPower(idx)` | PowerController | `cmd.boolVal` (on/off) | `dimmable-light/`, `smart-switch/`, `smart-plug/` | "Alexa, turn on the desk lamp" |
| `b` | `addBrightness(idx)` | BrightnessController | `cmd.intVal` 0..100 | `dimmable-light/`, `color-light/` | "Alexa, set the desk lamp to 40%" / "dim the desk lamp" |
| `c` | `addColor(idx)` | ColorController | `cmd.hue` 0..360, `cmd.sat`/`cmd.bri` 0..100 | `color-light/` | "Alexa, set the lamp to blue" |
| `k` | `addColorTemperature(idx)` | ColorTemperatureController | `cmd.intVal` 1000..10000 K | `tunable-white-light/` | "Alexa, set the light to 4000 kelvin" / "make it warmer" |
| `p` | `addPercentage(idx)` | PercentageController | `cmd.intVal` 0..100 | `percentage-dimmer/` | "Alexa, set the Hallway Dimmer to 30%" |
| `w` | `addPowerLevel(idx)` | PowerLevelController | `cmd.intVal` 0..100 | `power-level/` | "Alexa, set the workshop heater power to 70%" |
| `l` | `addLock(idx)` | LockController | `cmd.boolVal` (true = LOCKED) | `door-lock/` | "Alexa, lock the front door" / "is the front door locked?" |
| `d` | `addContactSensor(idx)` | ContactSensor (read-only) | `CapState.b` (true = DETECTED/open) | `window-sensor/` | "Alexa, is the window open?" |
| `v` | `addMotionSensor(idx)` | MotionSensor (read-only) | `CapState.b` (true = DETECTED) | `motion-sensor/` | "Alexa, is there motion in the hallway?" |
| `e` | `addTemperatureSensor(idx)` | TemperatureSensor (read-only) | `CapState.tempDeci` (tenths) + `CapState.scale` | `temperature-sensor/`, `thermostat/`, `hvac-thermostat/` | "Alexa, what's the temperature in the office?" |
| `n` | `addHumiditySensor(idx)` | RangeController "Humidity" (read-only %) | `CapState.i` 0..100 | `humidity-sensor/` | "Alexa, what's the humidity in the greenhouse?" |
| `x` | `addEventDetectionSensor(idx)` | EventDetectionSensor (read-only, proactive) | `CapState.b` (true = DETECTED) | `doorbell/` | (routines: "When someone is detected …") |
| `z` | `addDoorbell(idx)` | DoorbellEventSource (proactive event) | set `CapState.b = true` + `reportState()` fires a DoorbellPress | `doorbell/` | "Alexa, announce when someone is at the front door" |
| `C` | `addCamera(idx)` | CameraStreamController (stateless singleton) | no control — the Lambda renders the snapshot `imageUri` | `camera/` | "Alexa, show the front camera" |
| `h` | `addThermostat(idx)` | ThermostatController | `cmd.sub` (0 setpoint → `cmd.tempDeci`/`cmd.scale`; 1 mode → `cmd.mode`) | `thermostat/`, `hvac-thermostat/` | "Alexa, set the thermostat to 22 degrees" / "to cool" |
| `s` | `addScene(idx)` | SceneController (momentary) | `cmd.boolVal` (true = Activate) | `scene-controller/` | "Alexa, turn on movie night" |
| `a` | `addSecurityPanel(idx)` | SecurityPanelController | `cmd.mode` = armState (DISARMED / ARMED_AWAY / ARMED_STAY / ARMED_NIGHT) | `security-panel/` | "Alexa, arm the alarm in stay mode" / "disarm the alarm" |
| `u` | `addSpeaker(idx)` | Speaker | `cmd.intVal` volume 0..100, `cmd.boolVal` muted | `speaker/`, `media-player/` | "Alexa, set the volume to 30 on the speaker" / "mute" |
| `y` | `addPlayback(idx)` | PlaybackController (momentary) | `cmd.intVal` op enum: 1 Play 2 Pause 3 Stop 4 Next 5 Previous 6 Rewind 7 FastForward 8 StartOver | `media-player/` | "Alexa, pause the TV" / "next on the TV" |
| `g` | `addStepSpeaker(idx)` | StepSpeaker (momentary) | `cmd.sub` (0 AdjustVolume → `cmd.intVal` signed steps; 1 SetMute → `cmd.boolVal`) | `step-speaker/` | "Alexa, turn up the volume on the receiver" / "volume down 3" |
| `i` | `addTimeHold(idx)` | TimeHoldController (momentary) | `cmd.boolVal` (true = Hold, false = Resume) | `time-hold/` | "Alexa, pause the washer" / "resume the washer" |
| `j` | `addInput(idx)` | InputController | `cmd.intVal` = selected input **index** | `av-input/` | "Alexa, set the receiver input to HDMI 2" |
| `f` | `addChannel(idx)` | ChannelController | `cmd.sub` (0 ChangeChannel → `cmd.intVal` absolute; 1 SkipChannels → `cmd.intVal` signed) | `channel-tv/` | "Alexa, change the TV to channel 704" / "channel up" |
| `q` | `addEqualizer(idx)` | EqualizerController | `cmd.sub` (0 SetBands → `cmd.bass`/`cmd.mid`/`cmd.treble`, each −6..+6; 1 SetMode → `cmd.mode` MOVIE/MUSIC/NIGHT/SPORT/TV) | `equalizer-soundbar/` | "Alexa, set the bass to 4 on the soundbar" / "movie mode" |

### Instanced builders

| Code | Builder | Alexa interface | Value / `CtlCommand` field | Example | Sample utterance |
|---|---|---|---|---|---|
| `r` | `addRange(idx, instance, RangeConfig)` | RangeController | `cmd.intVal` (validated to `cfg.min..cfg.max`) | `ceiling-fan/`, `roller-blind/`, `curtain/` | "Alexa, set the fan speed to 7" / "open the curtains" |
| `m` | `addMode(idx, instance, ModeConfig)` | ModeController | `cmd.mode` = selected mode VALUE string | `ceiling-fan/`, `garage-door/`, `hvac-thermostat/` | "Alexa, set the fan direction to reverse" / "open the garage door" |
| `t` | `addToggle(idx, instance, ToggleConfig)` | ToggleController | `cmd.boolVal` (on/off) | `ceiling-fan/`, `hvac-thermostat/` | "Alexa, turn on the fan oscillate" |

`addRange`/`addMode`/`addToggle` **return the wire instance index** — save it and compare
against `cmd.instance` in your callback to know which instance was hit (indices are assigned
in declaration order). Singleton builders return `0` (unused).

---

## The control callback contract

```cpp
typedef bool (*ControlHandler)(const automatica::CtlCommand& cmd, void* ctx);
home.onControl(onControl, optionalCtx);
```

**Dispatch:** `cmd.code` is the capability code (first-level switch). For instanced caps
(`r`/`m`/`t`) also compare `cmd.instance` to the index the builder returned; for singletons
`cmd.instance == kNoInstance` and is not checked.

**`CtlCommand` fields** (only the ones relevant to `cmd.code` are populated):

| Field | Type | Meaning |
|---|---|---|
| `status` | int | `CtlStatus`; you only ever see a command on `CTL_OK` |
| `idx` | int | endpoint index (== Alexa idx) |
| `code` | char | capability code |
| `instance` | int | wire instance index, `kNoInstance` (−1) for singletons |
| `boolVal` | bool | `o`/`l`/`t`/`s`/`i` (and `g`/`u` mute) |
| `intVal` | int | `b`/`k`/`p`/`w`/`r`/`j`, `u` volume, `y` op enum, `f`/`g` signed value |
| `hue`,`sat`,`bri` | int | `c` color (hue 0..360, sat/bri 0..100) |
| `mode` | string | `m` mode value, `h` thermostat mode, `a` armState, `q` EQ mode |
| `sub` | int | sub-op for `h`/`g`/`f`/`q` |
| `tempDeci`,`scale` | int,string | `h` setpoint (tenths of a degree) + scale (CELSIUS/FAHRENHEIT/KELVIN) |
| `bass`,`mid`,`treble` | int | `q` SetBands levels, each −6..+6 |

**Return semantics:** `true` = handled (the facade applies the value to its state model and
calls `reportState()` to publish the change). `false` = failed → the facade returns
`CTL_ERR_CALLBACK` (−5) and the Lambda surfaces an Alexa error. Read-only sensors return
`false` for every directive. The library validates the command before calling you —
out-of-range values never reach the callback.

**`automaticaCtl` return statuses (`CtlStatus`):**

| Value | Name | When |
|---|---|---|
| `0` | `CTL_OK` | handled and applied |
| `-1` | `CTL_ERR_PARSE` | bad Ascii85 / truncated blob / unknown cap |
| `-2` | `CTL_ERR_BAD_INDEX` | no such endpoint |
| `-3` | `CTL_ERR_BAD_CODE` | capability not declared on the endpoint |
| `-4` | `CTL_ERR_OUT_OF_RANGE` | value out of range / not a valid mode |
| `-5` | `CTL_ERR_CALLBACK` | your handler returned `false` |
| `-6` | `CTL_ERR_BAD_INSTANCE` | code present but not at that instance index |

---

## Initial state & sensors (`reportState`)

State lives in the facade's model and is reflected in the manifest and the first snapshot.
Set it **before `begin()`** with `setInitialState(idx, code, instance, CapState)`. For
**sensors** (read-only `d`/`v`/`e`/`n`/`x`) there is no control path: poll the hardware in
`loop()`, update the stored state on a change, and call `reportState(idx)` — the next
`home.loop()` publishes the coalesced, latest-wins snapshot (≤1/sec).

```cpp
// window-sensor poll loop (read-only ContactSensor 'd')
bool open = readReed();
if (open != prevOpen) {
    prevOpen = open;
    CapState s; s.b = open;
    home.setInitialState(gWindowEp, kCapContactSensor, kNoInstance, s);
    home.reportState(gWindowEp);   // queue a proactive state report
}
```

**Momentary** capabilities (`s` Scene, `y` Playback, `g` StepSpeaker, `i` TimeHold) persist
no state — act on the command in the callback; do not call `setInitialState`/`reportState`.

---

## Singleton vs instanced — the rule

- **Singleton** (`o b c k p w l d v e n x z h s a u y g i j f q`): at most one per endpoint.
  Addressed by `cmd.code`; `cmd.instance == kNoInstance`. The builder's return value is unused.
- **Instanced** (`r m t`): many per endpoint, each a distinct *named* instance (e.g.
  `"Fan.Speed"`, `"Fan.Oscillate"`, `"Fan.Direction"`). The builder returns a wire instance
  index (assigned in declaration order) that you MUST capture and compare against
  `cmd.instance`.

```cpp
int gSpeedInst = home.addRange (fan, "Fan.Speed",     speed);
int gOscInst   = home.addToggle(fan, "Fan.Oscillate", osc);
int gDirInst   = home.addMode  (fan, "Fan.Direction", dir);
// ...in onControl:
case kCapRange:  if (cmd.instance == gSpeedInst) { applySpeed(cmd.intVal); return true; } break;
case kCapToggle: if (cmd.instance == gOscInst)   { applyOscillate(cmd.boolVal); return true; } break;
case kCapMode:   if (cmd.instance == gDirInst)   { applyDirection(cmd.mode); return true; } break;
```

---

## Gotchas

- **The `.ino` preprocessor prototype rule.** The Arduino/Particle preprocessor emits a
  forward prototype for each function **above** your `using namespace automatica;` line. So
  fully-qualify the callback's parameter type as `automatica::CtlCommand`, or the generated
  prototype won't compile.
- **Don't name the facade `automatica`** — it collides with the namespace. Use `home`.
- **Construct the `AutomaticaCloud` adapter** at global scope right after the facade — its
  constructor calls `home.setCloudPort(this)`. Without it nothing is wired to the cloud.
- **Capture instanced builder return values** — the most common bug is hard-coding instance
  indices `0,1,2` instead of saving what the builder returned.
- **Endpoint declaration order is the device identity** and must be stable across reboots
  (the array index *is* the Alexa idx). Endpoint `id` must match `^[a-z0-9_-]{1,24}$`.
- **Read-only sensors** never accept directives — `onControl` returns `false`; drive them
  via `reportState()` from `loop()`.
- **Publish budget.** State is coalesced latest-wins and rate-limited to ≤1/sec;
  `reportState()` only marks dirty, `loop()` publishes.
- **Particle variable cap.** A device allows ~20 `Particle.variable`s, which bounds total
  manifest pages; each page is ≤ 864 chars.
- **Color brightness is the shared `b` capability** — declare `addBrightness` alongside
  `addColor`.

---

## Examples

Each `examples/<name>/<name>.ino` is one runnable sketch for one archetype; every header
comment names the archetype, its capabilities, and the exact Alexa utterances it enables.

| Example | Capabilities | Shows |
|---|---|---|
| `dimmable-light/` | `o`+`b` | on/off + brightness |
| `color-light/` | `o`+`b`+`c` | RGB color (shared `b` brightness) |
| `tunable-white-light/` | `o`+`b`+`k` | color temperature (Kelvin) |
| `percentage-dimmer/` | `o`+`p` | PercentageController |
| `power-level/` | `o`+`w` | PowerLevelController |
| `step-speaker/` | `o`+`g` | StepSpeaker (momentary, relative volume) |
| `time-hold/` | `o`+`i` | TimeHoldController (momentary, Hold/Resume) |
| `av-input/` | `o`+`j` | InputController (SelectInput by index) |
| `humidity-sensor/` | `n` | Humidity (read-only %) |
| `channel-tv/` | `o`+`f` | ChannelController (ChangeChannel/SkipChannels) |
| `equalizer-soundbar/` | `o`+`q` | EqualizerController (bands + preset modes) |
| `hvac-thermostat/` | `h`+`m`+`t`+`e` | thermostat (ECO) + fan mode + aux heat |
| `doorbell/` | `z`+`x` | DoorbellEventSource + EventDetectionSensor (proactive) |
| `camera/` | `C` | CameraStreamController (stateless snapshot) |
| `smart-switch/` | `o` | on/off `SWITCH` |
| `smart-plug/` | `o` | `SMARTPLUG`/`OUTLET` |
| `door-lock/` | `l` | LockController |
| `window-sensor/` | `d` | read-only ContactSensor + `reportState` |
| `motion-sensor/` | `v` | read-only MotionSensor |
| `temperature-sensor/` | `e` | read-only TemperatureSensor |
| `thermostat/` | `h`+`e` | setpoint + mode + current temp |
| `scene-controller/` | `s` | momentary SceneController |
| `security-panel/` | `a` | armState |
| `speaker/` | `u` | volume + mute |
| `media-player/` | `y`+`u` | transport ops + volume |
| `ceiling-fan/` | `o`+`r`+`t`+`m` | power + speed + oscillate + direction (canonical instanced) |
| `roller-blind/` | `r`+semantics | position with open/close/raise/lower |
| `curtain/` | `r`+semantics | `CURTAIN` position |
| `garage-door/` | `m`+semantics | open/closed + `GARAGE_DOOR` PIN |
| `multi-endpoint/` | mixed | several endpoints on one MCU |

---

## Architecture

The core (`src/automatica.{h,cpp}`) is **Device-OS-free** — only `std::string`/`int`/
`std::vector`, no `Particle.h` or Arduino `String` — so it compiles and is unit-tested on the
host with Catch2. It owns the Ascii85 codec and the binary manifest/control/state
(de)serializers, which byte-match a Go reference codec and a shared set of fixtures. The
cloud primitives sit behind `CloudPort` (`src/CloudPort.h`); the on-device adapter
`AutomaticaCloud` (`src/AutomaticaCloud.h`, compiled only when `PARTICLE`/`SPARK`/`ARDUINO`
is defined) maps them onto Device-OS `Particle.variable`/`.function`/`.publish`, and a test
double is injected on the host.

`automatica` is one library of the broader **automatica** platform (a self-hosted Alexa
Smart Home Lambda + skill that drives these devices). The companion Lambda and the wire
contract are maintained alongside it.

---

## 🎧 Podcast

A short, beginner-friendly audio series on building with automatica — what to type, what
to say to Alexa, and the gotchas to avoid. Subscribe to the **automatica For Particle**
feed: https://podcasts.jeremy.ninja/api/v1/feeds/5a4c5841-215e-463f-a7e5-69152cd739f5/feed.xml

| Episode | Listen |
|---|---|
| **Ep 1 — Your First Voice-Controlled Device** | https://dc9qypb8dyvso.cloudfront.net/automatica-ep1-your-first-voice-controlled-device.m4a |
| **Ep 2 — Lights, Dimmers & Color** | https://dc9qypb8dyvso.cloudfront.net/automatica-ep2-lights-dimmers-and-color.m4a |
| **Ep 3 — Sensors, Locks & the Home** | https://dc9qypb8dyvso.cloudfront.net/automatica-ep3-sensors-locks-and-the-home.m4a |
| **Ep 4 — One Device, Many Controls** | https://dc9qypb8dyvso.cloudfront.net/automatica-ep4-one-device-many-controls.m4a |

---

## License

[MIT](LICENSE) © Jeremy Proffitt
