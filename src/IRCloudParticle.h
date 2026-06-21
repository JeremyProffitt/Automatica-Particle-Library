// IRCloudParticle.h — on-device IRCloudPort adapter backed by Particle Device-OS.
//
// Cloned from automatica's AutomaticaCloud.h. This is the ONLY AutomaticaIR file
// that includes Particle.h, and it is compiled only when building for a Particle
// target (PARTICLE is defined by the Device-OS toolchain). Host Catch2 builds never
// see this translation unit, so the core stays Device-OS-free.
//
// Usage in a sketch:
//   IRHardwarePhoton  hw;
//   IRCloudParticle   cloud;
//   AutomaticaIR      blaster(cloud, hw);
//   void setup() { blaster.begin(); }
//   void loop()  { blaster.loop(); }
#ifndef AUTOMATICA_IR_CLOUD_PARTICLE_H
#define AUTOMATICA_IR_CLOUD_PARTICLE_H

#if defined(PARTICLE) || defined(SPARK) || defined(ARDUINO)

#include "Particle.h"
#include "AutomaticaIR.h"

#include <string>
#include <vector>

namespace automatica_ir {

// Adapter mapping IRCloudPort onto Particle.variable / .function / .publish.
//
// Particle.variable (string form) requires a pointer to a String/std::string that
// stays alive. The core owns its page strings (std::string) and passes us pointers;
// we copy each into a Particle String kept here so its lifetime and type match what
// Device-OS wants, and refresh() re-copies when the core mutates a page (capture
// completes / irState changes). kMaxVars is reserved up front so each mirror's
// address is stable (Particle.variable binds to &m.mirror, so the backing vector
// must NOT reallocate after a bind).
class IRCloudParticle : public IRCloudPort {
public:
    // irState (1) + irCapture0..N (kCapturePageCount). Particle caps a device at
    // ~20 variables; reserve a little headroom.
    static const size_t kMaxVars = (size_t)kCapturePageCount + 4;

    IRCloudParticle() : fnBegin_(0), fnChunk_(0), fnSend_(0), fnRecord_(0),
                        ctxBegin_(0), ctxChunk_(0), ctxSend_(0), ctxRecord_(0) {
        vars_.reserve(kMaxVars);
    }

    void registerVariable(const std::string& name, const std::string* value) {
        vars_.push_back(VarMirror());
        VarMirror& m = vars_.back();
        m.name = String(name.c_str());
        m.src = value;
        m.mirror = String(value->c_str());
        // m's address is stable because vars_ was reserved to kMaxVars in the ctor.
        Particle.variable(m.name, m.mirror);
    }

    void registerFunction(const std::string& name, IRFn fn, void* ctx) {
        // Particle.function needs a member fn taking String; trampoline per name.
        if (name == "irBegin") {
            fnBegin_ = fn; ctxBegin_ = ctx; nameBegin_ = String(name.c_str());
            Particle.function(nameBegin_, &IRCloudParticle::beginThunk, this);
        } else if (name == "irChunk") {
            fnChunk_ = fn; ctxChunk_ = ctx; nameChunk_ = String(name.c_str());
            Particle.function(nameChunk_, &IRCloudParticle::chunkThunk, this);
        } else if (name == "irSend") {
            fnSend_ = fn; ctxSend_ = ctx; nameSend_ = String(name.c_str());
            Particle.function(nameSend_, &IRCloudParticle::sendThunk, this);
        } else if (name == "irRecord") {
            fnRecord_ = fn; ctxRecord_ = ctx; nameRecord_ = String(name.c_str());
            Particle.function(nameRecord_, &IRCloudParticle::recordThunk, this);
        }
    }

    bool publish(const std::string& name, const std::string& data) {
        return Particle.publish(String(name.c_str()), String(data.c_str()), PRIVATE);
    }

    bool connected() { return Particle.connected(); }

    unsigned long millis() { return ::millis(); }

    // The core mutates a backing std::string in place then calls refresh(name);
    // re-copy it into the bound Particle String mirror so the cloud sees the update.
    void refresh(const std::string& name) {
        for (size_t i = 0; i < vars_.size(); ++i) {
            if (vars_[i].name == String(name.c_str())) {
                vars_[i].mirror = String(vars_[i].src->c_str());
                return;
            }
        }
    }

private:
    struct VarMirror {
        String             name;
        const std::string* src;     // core-owned source
        String             mirror;  // Particle-typed copy bound to the variable
        VarMirror() : src(0) {}
    };

    int beginThunk(String arg)  { return fnBegin_  ? fnBegin_(std::string(arg.c_str()), ctxBegin_)  : IR_ERR_PARSE; }
    int chunkThunk(String arg)  { return fnChunk_  ? fnChunk_(std::string(arg.c_str()), ctxChunk_)  : IR_ERR_PARSE; }
    int sendThunk(String arg)   { return fnSend_   ? fnSend_(std::string(arg.c_str()), ctxSend_)    : IR_ERR_PARSE; }
    int recordThunk(String arg) { return fnRecord_ ? fnRecord_(std::string(arg.c_str()), ctxRecord_): IR_ERR_PARSE; }

    std::vector<VarMirror> vars_;

    String nameBegin_, nameChunk_, nameSend_, nameRecord_;
    IRFn   fnBegin_, fnChunk_, fnSend_, fnRecord_;
    void*  ctxBegin_; void* ctxChunk_; void* ctxSend_; void* ctxRecord_;
};

}  // namespace automatica_ir

#endif  // PARTICLE
#endif  // AUTOMATICA_IR_CLOUD_PARTICLE_H
