#include "mips_soft_tlb.h"

#include <cstddef>

#include <cerfengine/mips/mips_cpu.h>
#include <cerfengine/mips/mips_cpu_state.h>
#include <cerfengine/mips/mips_mmu.h>
#include <cerfengine/physical_map.h>
#include "mips_translation_cache.h"

namespace cerfengine {

CERFENGINE_REGISTER(MipsSoftTlb);

void MipsSoftTlb::OnReady() {
    cpu_       = &engine_.Get<MipsCpu>();
    cpu_state_ = cpu_->State();
    mmu_       = &engine_.Get<MipsMmu>();
    cache_     = &engine_.Get<MipsTranslationCache>();
    map_       = &engine_.Get<PhysicalMap>();
    state_     = mmu_->SoftTlb();
    map_->AddInvalidateListener(&FlushOnRemap, this);
}

void MipsSoftTlb::FlushOnRemap(void* tlb) {
    static_cast<MipsSoftTlb*>(tlb)->FlushAll();
}

void MipsSoftTlb::DescribeModel(cerfir::SoftTlbModel& t) {
    t.privileges = kBlockPrivilegeCount;
    t.fold_mask  = 0u;
    t.page_shift = kMipsSoftTlbPageShift;
    t.set_mask   = kMipsSoftTlbSetMask;
    t.set_bytes  = kMipsSoftTlbWays * static_cast<uint32_t>(sizeof(MipsSoftTlbEntry));
    for (uint32_t p = 0; p < kBlockPrivilegeCount; ++p) {
        t.unit_aux[p] = static_cast<int32_t>(offsetof(MipsSoftTlbState, units) +
                                             p * sizeof(MipsSoftTlbUnit));
    }
    t.entry_tag      = static_cast<int32_t>(offsetof(MipsSoftTlbEntry, tag));
    t.entry_addend   = static_cast<int32_t>(offsetof(MipsSoftTlbEntry, addend));
    t.entry_pa       = static_cast<int32_t>(offsetof(MipsSoftTlbEntry, pa_page));
    t.entry_context  = static_cast<int32_t>(offsetof(MipsSoftTlbEntry, context));
    t.entry_global   = static_cast<int32_t>(offsetof(MipsSoftTlbEntry, global));
    t.entry_writable = static_cast<int32_t>(offsetof(MipsSoftTlbEntry, writable));
    t.context_aux    = static_cast<int32_t>(offsetof(MipsSoftTlbState, context));
    t.watch_writes   = false;
    t.io_tag_bit     = 0u;
    t.miss_helper[static_cast<size_t>(cerfir::TlbAccess::kRead)] =
        reinterpret_cast<const void*>(&MissRead);
    t.miss_helper[static_cast<size_t>(cerfir::TlbAccess::kWrite)] =
        reinterpret_cast<const void*>(&MissWrite);
    t.miss_helper[static_cast<size_t>(cerfir::TlbAccess::kReadWrite)] =
        reinterpret_cast<const void*>(&MissWrite);
    t.context = this;
}

void MipsSoftTlb::DemoteHostPage(const uint8_t* host_page) {
    const uintptr_t page = reinterpret_cast<uintptr_t>(host_page);
    for (MipsSoftTlbUnit& unit : state_->units) {
        for (MipsSoftTlbEntry& e : unit.entries) {
            if (e.tag != kMipsSoftTlbInvalidTag && static_cast<uintptr_t>(e.tag) + e.addend == page) {
                e.writable = 0u;
            }
        }
    }
}

uint8_t* CERFENGINE_FASTCALL MipsSoftTlb::MissRead(uint32_t va, MipsSoftTlb* tlb) {
    return tlb->Miss<false>(va);
}

uint8_t* CERFENGINE_FASTCALL MipsSoftTlb::MissWrite(uint32_t va, MipsSoftTlb* tlb) {
    return tlb->Miss<true>(va);
}

template <bool kWrite>
uint8_t* MipsSoftTlb::Miss(uint32_t va) {
    const uint32_t   privilege = cpu_->Privilege();
    MipsSoftTlbUnit& unit      = state_->units[privilege];
    const uint32_t   base      = MipsSoftTlbSetBase(va);
    const uint32_t   tag       = va & kMipsSoftTlbPageMask;
    if (const MipsSoftTlbEntry* hit = ProbeVictims(unit, base, tag, kWrite)) {
        return reinterpret_cast<uint8_t*>(static_cast<uintptr_t>(va) + hit->addend);
    }
    uint32_t pa = 0;
    if (mmu_->TranslateAs(privilege, cpu_state_, va, kWrite ? MipsAccess::kWrite : MipsAccess::kRead,
                          &pa) != MipsTlbResult::kMatch) {
        return nullptr;
    }
    uint8_t* const host     = kWrite ? map_->Write(pa) : map_->Read(pa);
    if (host == nullptr) return nullptr;
    const bool     writable = StoreFastPath(privilege, va, pa, host);
    if (kWrite && !writable) return nullptr;
    Fill(privilege, va, pa, host, writable);
    return host;
}

MipsSoftTlbEntry* MipsSoftTlb::ProbeVictims(MipsSoftTlbUnit& unit, uint32_t base, uint32_t tag,
                                            bool need_write) {
    MipsSoftTlbEntry* const set = &unit.entries[base];
    for (uint32_t w = 1; w < kMipsSoftTlbWays; ++w) {
        if (set[w].tag != tag || (need_write && set[w].writable == 0u)) continue;
        const MipsSoftTlbEntry hit = set[w];
        for (uint32_t k = w; k > 0; --k) set[k] = set[k - 1u];
        set[0] = hit;
        return &set[0];
    }
    return nullptr;
}

bool MipsSoftTlb::StoreFastPath(uint32_t privilege, uint32_t va, uint32_t pa, const uint8_t* host) {
    uint32_t write_pa = 0;
    if (mmu_->TranslateAs(privilege, cpu_state_, va, MipsAccess::kWrite, &write_pa) !=
            MipsTlbResult::kMatch ||
        write_pa != pa || map_->Write(pa) != host) {
        return false;
    }
    return cache_->PageStoresSkipInvalidation(host - (va & ~kMipsSoftTlbPageMask));
}

void MipsSoftTlb::Fill(uint32_t privilege, uint32_t va, uint32_t pa, uint8_t* host, bool writable) {
    bool mapped = false;
    if (!mmu_->PageTranslationUniform(privilege, cpu_state_, va, &mapped)) return;
    const uint32_t   tag  = va & kMipsSoftTlbPageMask;
    const uint32_t   base = MipsSoftTlbSetBase(va);
    MipsSoftTlbUnit& unit = state_->units[privilege];
    MipsSoftTlbEntry* const set = &unit.entries[base];
    uint32_t slot = kMipsSoftTlbWays - 1u;
    for (uint32_t w = 0; w < kMipsSoftTlbWays; ++w) {
        if (set[w].tag == tag) {
            slot = w;
            break;
        }
    }
    for (uint32_t k = slot; k > 0; --k) set[k] = set[k - 1u];
    MipsSoftTlbEntry& e = set[0];
    e.tag      = tag;
    e.addend   = reinterpret_cast<uintptr_t>(host - (va & ~kMipsSoftTlbPageMask)) -
                 static_cast<uintptr_t>(tag);
    e.pa_page  = pa & kMipsSoftTlbPageMask;
    e.context  = 0u;
    e.global   = 1u;
    e.writable = writable ? 1u : 0u;
    e.mapped   = mapped ? 1u : 0u;
    if (mapped) {
        const uint32_t set_index = base / kMipsSoftTlbWays;
        unit.mapped_sets[set_index / 32u] |= 1u << (set_index & 31u);
    }
}

}
