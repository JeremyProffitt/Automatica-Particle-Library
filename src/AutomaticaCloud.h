// AutomaticaCloud.h — on-device CloudPort adapter backed by Particle Device-OS.
//
// This is the ONLY file that includes Particle.h, and it is compiled only when
// building for a Particle target (PARTICLE is defined by the Device-OS
// toolchain). Host Catch2 builds never see this translation unit, so the core
// stays Device-OS-free.
//
// Usage in a sketch:
//   Automatica automatica;
//   automatica::AutomaticaCloud cloud(automatica);   // wires Particle surface
//   void setup() { ... addEndpoint ...; automatica.begin(); }
//   void loop()  { automatica.loop(); }
// (AutomaticaCloud's constructor calls automatica.setCloudPort(this).)
#ifndef AUTOMATICA_CLOUD_H
#define AUTOMATICA_CLOUD_H

#if defined(PARTICLE) || defined(SPARK) || defined(ARDUINO)

#include "Particle.h"
#include "automatica.h"

#include <string>
#include <vector>

namespace automatica {

// Adapter that maps CloudPort onto Particle.variable / .function / .publish.
//
// Particle.variable (string form) requires a pointer to a String/std::string
// that stays alive. The core owns its page strings (std::string) and passes us
// pointers; we copy each into a Particle String kept here so its lifetime and
// type match what Device-OS wants, and refresh them whenever the value changes.
// For automatica the manifest is built once in begin(), so a one-time copy is
// sufficient; we also expose refresh() if a future feature rebuilds it.
class AutomaticaCloud : public CloudPort {
public:
    // kMaxVars bounds how many manifest variables we can register. Particle
    // caps a device at ~20 variables; reserving up front keeps each mirror's
    // address stable (Particle.variable binds to &m.mirror, so the backing
    // vector must NOT reallocate after a bind).
    static const size_t kMaxVars = 20;

    explicit AutomaticaCloud(Automatica& core) : core_(core), fn_(0), fnCtx_(0) {
        vars_.reserve(kMaxVars);
        core_.setCloudPort(this);
    }

    void registerVariable(const std::string& name, const std::string* value) {
        // Keep a stable Particle String mirror of the core's std::string.
        vars_.push_back(VarMirror());
        VarMirror& m = vars_.back();
        m.name = String(name.c_str());
        m.src = value;
        m.mirror = String(value->c_str());
        // m's address is stable because vars_ was reserved to kMaxVars in the
        // ctor, so Particle.variable's bind to &m.mirror stays valid.
        Particle.variable(m.name, m.mirror);
    }

    void registerFunction(const std::string& name, CtlFn fn, void* ctx) {
        fnName_ = String(name.c_str());
        fn_ = fn;
        fnCtx_ = ctx;
        // Particle.function needs a member fn taking String; trampoline below.
        Particle.function(fnName_, &AutomaticaCloud::ctlThunk, this);
    }

    bool publish(const std::string& name, const std::string& data) {
        return Particle.publish(String(name.c_str()), String(data.c_str()),
                                PRIVATE);
    }

    bool connected() { return Particle.connected(); }

    unsigned long millis() { return ::millis(); }

private:
    struct VarMirror {
        String             name;
        const std::string* src;    // core-owned source
        String             mirror; // Particle-typed copy bound to the variable
        VarMirror() : src(0) {}
    };

    int ctlThunk(String arg) {
        if (!fn_) return CTL_ERR_PARSE;
        std::string s(arg.c_str());
        return fn_(s, fnCtx_);
    }

    Automatica&            core_;
    std::vector<VarMirror> vars_;
    String                 fnName_;
    CtlFn                  fn_;
    void*                  fnCtx_;
};

}  // namespace automatica

#endif  // PARTICLE
#endif  // AUTOMATICA_CLOUD_H
