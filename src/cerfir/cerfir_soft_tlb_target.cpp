#include "cerfir_soft_tlb_target.h"

#include <cerfengine/engine.h>

#include "../emit_fault.h"
#include "../jit_code_arena.h"

namespace cerfengine::cerfir {

void BuildSoftTlbTarget(Engine& engine, JitCodeArena& arena, const GuestModel& model,
                        HostSoftTlb& tlb, HostGuestTarget& target, const char* owner) {
    uint8_t* const region = arena.AllocatePermanent(HostSoftTlb::RegionBytes(model.tlb.privileges));
    if (region == nullptr) {
        engine.Panic("%s: the code arena has no room for the TLB miss paths", owner);
    }
    uint8_t* stub_region = nullptr;
    if constexpr (HostLowering::kEntryStubBytes != 0u) {
        stub_region = arena.AllocatePermanent(HostLowering::kEntryStubBytes);
        if (stub_region == nullptr) {
            engine.Panic("%s: the code arena has no room for the block entry stub", owner);
        }
    }
    try {
        tlb.Build(model, region);
        target.entry_stub = HostLowering::BuildEntryStub(model.pinned, stub_region);
    } catch (const EmitFault& fault) {
        engine.Panic("%s", fault.what());
    }
    target.model = &model;
    target.tlb   = &tlb;
}

}
