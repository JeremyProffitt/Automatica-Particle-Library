// camera.ino — automatica example
// ============================================================================
// DEVICE ARCHETYPE: a camera endpoint (e.g. a Particle device wired to a camera
// module, or a Particle controller fronting an IP camera) advertised to Alexa as a
// CAMERA via the addCamera() builder. ONE endpoint, ONE stateless singleton cap.
//
// ALEXA CAPABILITY EXPOSED (singleton, stateless — no instance, no state):
//   'C' Alexa.CameraStreamController — addCamera() advertises the endpoint as a
//        CAMERA (display category CAMERA). The Lambda renders the stream config and
//        answers InitializeCameraStreams with a (Phase-1) still-image snapshot
//        imageUri. There is NO automaticaCtl directive to the device for this cap —
//        it is stateless, so onControl() never receives it.
//
// SNAPSHOT CAPTURE IS OUT OF BAND (not automaticaCtl): the device captures a JPEG
// and HTTPS-PUTs it to a presigned URL that arrives over the cloud transport on
//   automatica/<device>/snapshot/request
// (Alexa/Lambda-driven). Capture itself is platform-specific hardware glue and is
// intentionally NOT part of the transport-agnostic core — see the ESP32 reference
// pipeline in esp32/automatica/examples/camera-snapshot for a full OV2640→S3 proof.
// This Particle example focuses on the one core concept: DECLARING the camera cap.
//
// EXACT ALEXA UTTERANCES / BEHAVIORS THIS ENABLES:
//   - "Alexa, show the Front Door camera" (on an Echo Show / Fire TV) → the Lambda
//     returns the latest snapshot imageUri for the endpoint.
//   (There are no Set/Adjust spoken commands — the cap is stateless.)
//
// GOTCHAS:
//   - addCamera() is a STATELESS SINGLETON: there is no CapState and no
//     setInitialState() for it (unlike sensors/lights). Do not call reportState()
//     for the camera cap — there is nothing to report.
//   - It is addressed by capability CODE (kCapCamera) with instance == kNoInstance.
//   - onControl() still must be registered (required for every endpoint), but it
//     never fires for the camera cap; reject anything unexpected by returning false.
//   - .ino preprocessor rule: auto-generated prototypes land ABOVE
//     'using namespace automatica;', so the callback parameter type must be
//     FULLY QUALIFIED (automatica::CtlCommand) for the generated prototype to compile.
// ============================================================================
#include "automatica.h"
#include "AutomaticaCloud.h"

using namespace automatica;

Automatica home;                 // the core facade; never name it 'automatica' (clashes with the namespace)
AutomaticaCloud cloud(home);     // Particle Device-OS adapter; its ctor calls home.setCloudPort(this)

int gEp;                         // endpoint index from addEndpoint()

// Control callback: the camera cap is stateless (no directive), so reject anything
// that ever reaches the device. Fully-qualify CtlCommand (the .ino preprocessor
// emits the prototype before 'using namespace automatica;').
static bool onControl(const automatica::CtlCommand& cmd, void*) {
    (void)cmd;
    return false;
}

void setup() {
    // Register the endpoint. Returns the endpoint index (== Alexa endpoint idx);
    // declaration order is the stable device identity and must not change across reboots.
    //   id   "front_cam"          — internal id, ^[a-z0-9_-]{1,24}$
    //   name "Front Camera"       — Alexa friendlyName
    //   desc "automatica camera"  — Alexa description
    //   cat  {"CAMERA"}           — Alexa displayCategory (matches the cap)
    gEp = home.addEndpoint("front_cam", "Front Camera", "automatica camera", {"CAMERA"});

    // Advertise the camera capability (stateless singleton; return value ignored —
    // addressed by code). No setInitialState() — there is no state for 'C'.
    home.addCamera(gEp);                 // 'C' Alexa.CameraStreamController

    home.onControl(onControl);           // register the (reject-all) callback; required for every endpoint
    home.begin();                        // build the manifest + register Particle var/function (call once)
}

void loop() {
    home.loop();                         // flush the initial manifest/snapshot publish

    // Snapshot capture is out-of-band: subscribe your camera glue to
    // automatica/<device>/snapshot/request and HTTPS-PUT the JPEG to the presigned
    // URL it carries. Nothing camera-related happens through automaticaCtl/onControl.
}
