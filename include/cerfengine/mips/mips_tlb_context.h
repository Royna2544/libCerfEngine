#pragma once

#include <cstdint>

#include "../isa_block_space.h"
#include "mips_soft_tlb_state.h"

namespace cerfengine {

struct MipsCpuState;

class MipsTlbContext {
public:
    void Bind(MipsCpuState* state, IsaBlockSpace* blocks32, IsaBlockSpace* blocks16,
              MipsSoftTlbState* soft_tlb) {
        state_    = state;
        blocks32_ = blocks32;
        blocks16_ = blocks16;
        soft_tlb_ = soft_tlb;
    }

    MipsCpuState* State() const { return state_; }

    void InvalidatePage(uint32_t page_va) {
        blocks32_->JumpCacheClearPage(page_va);
        blocks16_->JumpCacheClearPage(page_va);
        MipsSoftTlbInvalidateRange(soft_tlb_, page_va & kMipsSoftTlbPageMask, 1u << kMipsSoftTlbPageShift);
    }

    void InvalidateRange(uint32_t va, uint32_t span_bytes) {
        const uint32_t base = va & 0xFFFFF000u;
        const uint32_t span = (((va + span_bytes - 1u) | 0xFFFu) + 1u) - base;
        blocks32_->JumpCacheClearRange(base, span);
        blocks16_->JumpCacheClearRange(base, span);
        MipsSoftTlbInvalidateRange(soft_tlb_, base, span);
    }

    void InvalidateAddressSpace() {
        blocks32_->JumpCacheFlush();
        blocks16_->JumpCacheFlush();
        MipsSoftTlbFlushMapped(soft_tlb_);
    }

    void InvalidateMappedTranslations() { MipsSoftTlbFlushMapped(soft_tlb_); }

private:
    MipsCpuState*     state_    = nullptr;
    IsaBlockSpace*    blocks32_ = nullptr;
    IsaBlockSpace*    blocks16_ = nullptr;
    MipsSoftTlbState* soft_tlb_ = nullptr;
};

}
