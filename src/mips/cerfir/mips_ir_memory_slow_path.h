#pragma once

#include <cstdint>

#include <cerfengine/calling_convention.h>
#include <cerfengine/engine.h>

#include "../../cerfir/cerfir_guest_model.h"

namespace cerfengine {

class MipsCpu;
class MipsMemoryAccess;
struct MipsCpuState;

class MipsIrMemorySlowPath : public EngineService {
public:
    using EngineService::EngineService;

    bool ShouldRegister() override { return engine_.GetSetup().isa == Isa::kMips; }
    void OnReady() override;

    cerfir::MemorySlowModel SlowModel() const {
        return cerfir::MemorySlowModel{this, entries_};
    }

private:
    static MipsIrMemorySlowPath* Owner(const cerfir::MemorySlowDesc* desc) {
        return static_cast<MipsIrMemorySlowPath*>(const_cast<void*>(desc->context));
    }

    template <uint8_t kWidth, bool kSign>
    static uint64_t CERFENGINE_FASTCALL Load(uint32_t va, const cerfir::MemorySlowDesc* desc);
    template <uint8_t kWidth>
    static uint64_t CERFENGINE_FASTCALL Store(uint32_t va, const cerfir::MemorySlowDesc* desc,
                                              uint32_t value);
    static uint64_t CERFENGINE_FASTCALL UnmodeledShape(uint32_t va,
                                                       const cerfir::MemorySlowDesc* desc);

    template <uint8_t kWidth, bool kSign>
    void SetLoad();
    template <uint8_t kWidth>
    void SetStore();

    cerfir::SlowEntries entries_[cerfir::kSlowShapes]{};
    MipsCpu*            cpu_       = nullptr;
    MipsCpuState*       cpu_state_ = nullptr;
    MipsMemoryAccess*   access_    = nullptr;
};

}
