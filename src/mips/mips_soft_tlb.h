#pragma once

#include <cstdint>

#include <cerfengine/calling_convention.h>
#include <cerfengine/engine.h>
#include <cerfengine/mips/mips_soft_tlb_state.h>

#include "../cerfir/cerfir_guest_model.h"

namespace cerfengine {

class MipsCpu;
class MipsMmu;
class MipsTranslationCache;
class PhysicalMap;
struct MipsCpuState;

class MipsSoftTlb : public EngineService {
public:
    using EngineService::EngineService;

    bool ShouldRegister() override { return engine_.GetSetup().isa == Isa::kMips; }
    void OnReady() override;

    void DescribeModel(cerfir::SoftTlbModel& t);

    MipsSoftTlbState* State() { return state_; }

    void FlushAll() { MipsSoftTlbFlushAll(state_); }

    void DemoteHostPage(const uint8_t* host_page);

private:
    static uint8_t* CERFENGINE_FASTCALL MissRead(uint32_t va, MipsSoftTlb* tlb);
    static uint8_t* CERFENGINE_FASTCALL MissWrite(uint32_t va, MipsSoftTlb* tlb);
    static void FlushOnRemap(void* tlb);

    template <bool kWrite>
    uint8_t* Miss(uint32_t va);
    MipsSoftTlbEntry* ProbeVictims(MipsSoftTlbUnit& unit, uint32_t base, uint32_t tag,
                                   bool need_write);
    void Fill(uint32_t privilege, uint32_t va, uint32_t pa, uint8_t* host, bool writable);
    bool StoreFastPath(uint32_t privilege, uint32_t va, uint32_t pa, const uint8_t* host);

    MipsSoftTlbState*     state_     = nullptr;
    MipsCpu*              cpu_       = nullptr;
    MipsCpuState*         cpu_state_ = nullptr;
    MipsMmu*              mmu_       = nullptr;
    MipsTranslationCache* cache_     = nullptr;
    PhysicalMap*          map_       = nullptr;
};

}
