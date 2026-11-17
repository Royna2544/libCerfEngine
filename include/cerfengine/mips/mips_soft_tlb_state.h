#pragma once

#include <bit>
#include <cstdint>

#include "../isa_block_space.h"

namespace cerfengine {

struct MipsSoftTlbEntry {
    uint32_t  tag;
    uintptr_t addend;
    uint32_t  pa_page;
    uint8_t   context;
    uint8_t   global;
    uint8_t   writable;
    uint8_t   mapped;
#if UINTPTR_MAX > 0xFFFFFFFFu
    uint64_t  pad;
#endif
};

static_assert((sizeof(MipsSoftTlbEntry) & (sizeof(MipsSoftTlbEntry) - 1u)) == 0u,
              "a soft-TLB entry has a power-of-two size");

constexpr uint32_t kMipsSoftTlbPageShift  = 12u;
constexpr uint32_t kMipsSoftTlbPageMask   = ~((1u << kMipsSoftTlbPageShift) - 1u);
constexpr uint32_t kMipsSoftTlbWays       = 4u;
constexpr uint32_t kMipsSoftTlbSets       = 256u;
constexpr uint32_t kMipsSoftTlbSetMask    = kMipsSoftTlbSets - 1u;
constexpr uint32_t kMipsSoftTlbInvalidTag = 0xFFFFFFFFu;
constexpr uint32_t kMipsSoftTlbSetWords   = kMipsSoftTlbSets / 32u;

struct MipsSoftTlbUnit {
    MipsSoftTlbEntry entries[kMipsSoftTlbSets * kMipsSoftTlbWays];
    uint32_t         mapped_sets[kMipsSoftTlbSetWords];
};

struct MipsSoftTlbState {
    MipsSoftTlbUnit units[kBlockPrivilegeCount];
    uint32_t        context;
};

inline uint32_t MipsSoftTlbSetBase(uint32_t va) {
    return ((va >> kMipsSoftTlbPageShift) & kMipsSoftTlbSetMask) * kMipsSoftTlbWays;
}

inline void MipsSoftTlbFlushAll(MipsSoftTlbState* s) {
    for (MipsSoftTlbUnit& unit : s->units) {
        for (MipsSoftTlbEntry& e : unit.entries) e.tag = kMipsSoftTlbInvalidTag;
        for (uint32_t& word : unit.mapped_sets) word = 0u;
    }
}

inline void MipsSoftTlbFlushMapped(MipsSoftTlbState* s) {
    for (MipsSoftTlbUnit& unit : s->units) {
        for (uint32_t w = 0; w < kMipsSoftTlbSetWords; ++w) {
            for (uint32_t bits = unit.mapped_sets[w]; bits != 0u; bits &= bits - 1u) {
                const uint32_t set  = w * 32u + static_cast<uint32_t>(std::countr_zero(bits));
                MipsSoftTlbEntry* e = &unit.entries[set * kMipsSoftTlbWays];
                for (uint32_t way = 0; way < kMipsSoftTlbWays; ++way) {
                    if (e[way].mapped != 0u) e[way].tag = kMipsSoftTlbInvalidTag;
                }
            }
            unit.mapped_sets[w] = 0u;
        }
    }
}

inline void MipsSoftTlbInvalidateRange(MipsSoftTlbState* s, uint32_t base, uint32_t span_bytes) {
    const uint64_t pages = (static_cast<uint64_t>(base & ~kMipsSoftTlbPageMask) + span_bytes +
                            ~kMipsSoftTlbPageMask) >> kMipsSoftTlbPageShift;
    if (pages >= kMipsSoftTlbSets) {
        MipsSoftTlbFlushMapped(s);
        return;
    }
    for (uint32_t k = 0; k < pages; ++k) {
        const uint32_t tag = (base & kMipsSoftTlbPageMask) + (k << kMipsSoftTlbPageShift);
        const uint32_t set = MipsSoftTlbSetBase(tag);
        for (MipsSoftTlbUnit& unit : s->units) {
            MipsSoftTlbEntry* e = &unit.entries[set];
            for (uint32_t way = 0; way < kMipsSoftTlbWays; ++way) {
                if (e[way].tag == tag && e[way].mapped != 0u) e[way].tag = kMipsSoftTlbInvalidTag;
            }
        }
    }
}

}
