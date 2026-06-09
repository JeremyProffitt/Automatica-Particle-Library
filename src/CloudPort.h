// CloudPort.h — Device-OS-free seam over the Particle cloud surface.
//
// The automatica core (manifest builder, control parser, state debouncer) is
// written against this interface only; it never references Particle.h / Arduino
// String. On-device an AutomaticaCloud adapter (guarded by #if defined(PARTICLE))
// implements it with Particle.variable/function/publish; host Catch2 tests inject
// a test double. This is what lets the core compile and run on a desktop.
#ifndef AUTOMATICA_CLOUDPORT_H
#define AUTOMATICA_CLOUDPORT_H

#include <string>

namespace automatica {

// Function callback signature for the registered automaticaCtl handler, exposed
// here so the CloudPort can wire the real Particle.function to the core.
// arg is the verbatim "idx|code|value" string; returns the negative-int status.
typedef int (*CtlFn)(const std::string& arg, void* ctx);

// CloudPort abstracts the three Particle cloud primitives plus the millis clock.
// All methods take std::string / int so no Device-OS type leaks into the core.
class CloudPort {
public:
    virtual ~CloudPort() {}

    // Register a string read variable (Particle.variable). The core calls this
    // once per manifest page (automaticaManifest0..N). The pointed-to string must
    // remain valid for the lifetime of the variable (the core owns the storage).
    virtual void registerVariable(const std::string& name, const std::string* value) = 0;

    // Register the control function (Particle.function "automaticaCtl"). The
    // adapter must invoke fn(arg, ctx) when the cloud calls the function and
    // return fn's int result to the cloud.
    virtual void registerFunction(const std::string& name, CtlFn fn, void* ctx) = 0;

    // Publish a PRIVATE event (Particle.publish). Returns true on a successful
    // hand-off to the cloud. The core handles coalescing/debounce above this.
    virtual bool publish(const std::string& name, const std::string& data) = 0;

    // True once the device has an active cloud connection. Used to gate the
    // initial state snapshot (publish on connect).
    virtual bool connected() = 0;

    // Monotonic milliseconds since boot. Injectable so the debounce is testable
    // with a fake clock (no real sleeping in host tests).
    virtual unsigned long millis() = 0;
};

}  // namespace automatica

#endif  // AUTOMATICA_CLOUDPORT_H
