#pragma once

#include <cstdint>
#include <optional>

#include "../engine.h"
#include "../guest_engine.h"
#include "mips_cpu_state.h"

namespace cerfengine {

class GuardedBlockEntry;
class HostRequestChannel;
class MipsBlockCompiler;
class MipsCpu;
class MipsExceptionDelivery;
class MipsInterruptChannel;
class MipsMemoryAccess;
class MipsMmu;
class MipsSoftTlb;
class MipsTranslationCache;
class PhysicalMap;

class MipsJit : public GuestEngine {
public:
    using GuestEngine::GuestEngine;

    void OnReady() override;
    bool ShouldRegister() override {
        return engine_.GetSetup().isa == cerfengine::Isa::kMips;
    }

    MipsCpuState* CpuState() { return cpu_state_; }

    void SetExternalInterruptLevel(uint32_t ip_mask);
    void SetFixedWindow(uint32_t va, uint32_t pa, uint32_t size);
    void OnStateRestored();
    void PrepareStateSave();

    RunStatus Run() override;
    RunStatus RunSlice() override;
    bool      DeepSleep()    const override { return cpu_state_->deep_sleep != 0; }
    bool      ResetPending() const override;
    void      SetHostChainExit(bool requested) override;

    bool     GuestIrqMasked() const override;
    uint32_t Pc()           const override { return cpu_state_->pc; }
    void     PrintFatalDump() override;
    uint32_t PhysAddrMask() const override { return cpu_state_->phys_addr_mask; }

    std::optional<uint8_t*> PeekGuestVa(uint32_t va) override;
    uint8_t* ResolveGuestVaToHost(uint32_t va) override;
    bool     ResolveGuestVaToPa(uint32_t va, uint32_t* pa) override;

    void FlushTranslationCache() override;
    void SetResetPending(bool is_resume) override;
    void EnterDeepSleep() override;
    void ExitDeepSleep() override;
    void EnterIdleWait() override;
    void SetCoreClockStopped(bool stopped) override;
    void SetDmaRegion(uint32_t pa, uint32_t size) override;

private:
    enum class BlockPrep : uint8_t { kEnter, kReturn, kHalted };

    BlockPrep PrepareBlock(uint32_t& pc, void*& native);
    template <typename Fn>
    RunStatus EnterGuarded(Fn& fn, uint32_t& pc, const RunStatus& returned);
    bool      RunnerNeedsControl() const;
    void      DeliverReset();

    MipsCpuState* cpu_state_      = nullptr;
    uint32_t      predecessor_va_ = 0;

    MipsCpu*                cpu_           = nullptr;
    MipsMmu*                mmu_           = nullptr;
    MipsTranslationCache*   cache_         = nullptr;
    MipsBlockCompiler*      compiler_      = nullptr;
    MipsInterruptChannel*   channel_       = nullptr;
    MipsMemoryAccess*       mem_access_    = nullptr;
    MipsExceptionDelivery*  exceptions_    = nullptr;
    HostRequestChannel*     host_requests_ = nullptr;
    PhysicalMap*            map_           = nullptr;
    cerfengine::EngineHost* host_          = nullptr;
    GuardedBlockEntry*      block_entry_   = nullptr;
    MipsSoftTlb*            soft_tlb_      = nullptr;
    const void*             entry_stub_    = nullptr;
};

}
