#include "mips_ir_memory_slow_path.h"

#include <cerfengine/mips/mips_cpu.h>
#include <cerfengine/mips/mips_cpu_state.h>

#include "../mips_memory_access.h"

namespace cerfengine {

CERFENGINE_REGISTER(MipsIrMemorySlowPath);

namespace {

template <uint8_t kWidth, bool kSign>
uint32_t Extend(uint32_t value) {
    if constexpr (kSign && kWidth == 1u) {
        return static_cast<uint32_t>(static_cast<int32_t>(static_cast<int8_t>(value)));
    } else if constexpr (kSign && kWidth == 2u) {
        return static_cast<uint32_t>(static_cast<int32_t>(static_cast<int16_t>(value)));
    } else {
        return value;
    }
}

}

void MipsIrMemorySlowPath::OnReady() {
    cpu_       = &engine_.Get<MipsCpu>();
    cpu_state_ = cpu_->State();
    access_    = &engine_.Get<MipsMemoryAccess>();
    const void* const unmodeled = reinterpret_cast<const void*>(&UnmodeledShape);
    for (cerfir::SlowEntries& e : entries_) e = cerfir::SlowEntries{unmodeled, unmodeled};
    SetLoad<1u, false>();
    SetLoad<1u, true>();
    SetLoad<2u, false>();
    SetLoad<2u, true>();
    SetLoad<4u, false>();
    SetLoad<4u, true>();
    SetStore<1u>();
    SetStore<2u>();
    SetStore<4u>();
}

template <uint8_t kWidth, bool kSign>
void MipsIrMemorySlowPath::SetLoad() {
    const void* const fn = reinterpret_cast<const void*>(&Load<kWidth, kSign>);
    entries_[cerfir::SlowShapeIndex(false, false, kWidth, kSign, false, false)] =
        cerfir::SlowEntries{fn, fn};
}

template <uint8_t kWidth>
void MipsIrMemorySlowPath::SetStore() {
    const void* const fn = reinterpret_cast<const void*>(&Store<kWidth>);
    entries_[cerfir::SlowShapeIndex(false, true, kWidth, false, false, false)] =
        cerfir::SlowEntries{fn, fn};
}

template <uint8_t kWidth, bool kSign>
uint64_t CERFENGINE_FASTCALL MipsIrMemorySlowPath::Load(uint32_t va,
                                                       const cerfir::MemorySlowDesc* desc) {
    MipsIrMemorySlowPath* const self = Owner(desc);
    self->cpu_state_->pc             = desc->guest_pc;
    const MipsMemoryHelpers& helpers = MipsMemoryAccess::HelpersFor(self->cpu_->Privilege());
    const uint32_t value = helpers.load[cerfir::HelperWidthIndex(kWidth)](va, self->access_);
    return static_cast<uint64_t>(Extend<kWidth, kSign>(value)) << 32;
}

template <uint8_t kWidth>
uint64_t CERFENGINE_FASTCALL MipsIrMemorySlowPath::Store(uint32_t va,
                                                        const cerfir::MemorySlowDesc* desc,
                                                        uint32_t value) {
    MipsIrMemorySlowPath* const self = Owner(desc);
    self->cpu_state_->pc             = desc->guest_pc;
    const MipsMemoryHelpers& helpers = MipsMemoryAccess::HelpersFor(self->cpu_->Privilege());
    helpers.store[cerfir::HelperWidthIndex(kWidth)](va, value, self->access_);
    return 0u;
}

uint64_t CERFENGINE_FASTCALL MipsIrMemorySlowPath::UnmodeledShape(uint32_t va,
                                                                  const cerfir::MemorySlowDesc* desc) {
    Owner(desc)->engine_.Panic("MipsIrMemorySlowPath: no slow entry for this access shape "
                               "(va=0x%08X pc=0x%08X)", va, desc->guest_pc);
}

}
