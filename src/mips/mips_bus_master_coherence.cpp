#include <cerfengine/mips/mips_bus_master_coherence.h>

#include <cerfengine/physical_map.h>
#include "mips_translation_cache.h"

#include <algorithm>

namespace cerfengine {

CERFENGINE_REGISTER(MipsBusMasterCoherence);

void MipsBusMasterCoherence::OnReady() {
    translations_ = &engine_.Get<MipsTranslationCache>();
    map_ = &engine_.Get<PhysicalMap>();
}

void MipsBusMasterCoherence::AfterWrite(uint32_t pa, uint32_t bytes) {
    if (static_cast<uint64_t>(pa) + bytes > 0x100000000ull) {
        engine_.Halt(HaltReason::kUnpredictable, "MIPS bus-master write wraps physical space");
    }
    while (bytes != 0u) {
        const uint32_t count = std::min(bytes, 0x1000u - (pa & 0xFFFu));
        uint8_t* host = map_->Write(pa);
        if (!host) engine_.Halt(HaltReason::kUnmappedPhysical, "MIPS bus-master write pa=%08X", pa);
        translations_->InvalidateOnRamWrite(host, count);
        bytes -= count;
        pa += count;
    }
}

}
