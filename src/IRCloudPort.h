// IRCloudPort.h — Device-OS-free seam over the Particle cloud surface.
//
// Cloned from automatica's CloudPort.h. The AutomaticaIR core (chunk reassembler,
// pulse codec, capture state machine) is written against this interface only; it
// never references Particle.h / Arduino String. On-device an IRCloudParticle
// adapter (guarded by #if defined(PARTICLE)) implements it with
// Particle.variable/function/publish; host Catch2 tests inject a FakeCloudPort.
// This is what lets the core compile and run on a desktop with PARTICLE undefined.
#ifndef AUTOMATICA_IR_CLOUDPORT_H
#define AUTOMATICA_IR_CLOUDPORT_H

#include <string>

namespace automatica_ir {

// Function callback signature for every registered IR Particle.function
// (irBegin/irChunk/irSend/irRecord). `arg` is the verbatim cloud-supplied string;
// the return value is the negative-int status (or a non-negative result code) that
// flows straight back to the cloud caller (the Lambda).
typedef int (*IRFn)(const std::string& arg, void* ctx);

// IRCloudPort abstracts the Particle cloud primitives plus the millis clock. All
// methods take std::string / int so no Device-OS type leaks into the core.
class IRCloudPort {
public:
    virtual ~IRCloudPort() {}

    // Register a string read variable (Particle.variable). The core calls this
    // once per surface (irState, irCapture0..N). The pointed-to string must remain
    // valid for the lifetime of the variable (the core owns the storage and only
    // mutates the contents in place — the adapter must mirror it on refresh()).
    virtual void registerVariable(const std::string& name, const std::string* value) = 0;

    // Register an IR control function (Particle.function). The adapter invokes
    // fn(arg, ctx) when the cloud calls the function and returns fn's int result
    // to the cloud. Called once per function (irBegin/irChunk/irSend/irRecord).
    virtual void registerFunction(const std::string& name, IRFn fn, void* ctx) = 0;

    // Publish a PRIVATE event (Particle.publish). Returns true on a successful
    // hand-off to the cloud. AutomaticaIR does not publish in v0 (capture is
    // returned via paged variables, never published — too small / rate-limited),
    // but the seam is kept identical to automatica so the adapter mirrors cleanly.
    virtual bool publish(const std::string& name, const std::string& data) = 0;

    // True once the device has an active cloud connection.
    virtual bool connected() = 0;

    // Monotonic milliseconds since boot. Injectable so timeouts are testable with
    // a fake clock (no real sleeping in host tests).
    virtual unsigned long millis() = 0;

    // Notify the adapter that a previously-registered variable's backing string
    // contents changed, so a String-mirror adapter can refresh its copy. The core
    // calls this after it rewrites irState / an irCaptureN page. Adapters that bind
    // Particle.variable directly to a std::string need not act on it.
    virtual void refresh(const std::string& name) { (void)name; }
};

}  // namespace automatica_ir

#endif  // AUTOMATICA_IR_CLOUDPORT_H
