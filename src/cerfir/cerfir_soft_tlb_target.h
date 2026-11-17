#pragma once

#include "cerfir_guest_model.h"
#include "cerfir_host_lowering.h"

namespace cerfengine {
class Engine;
class JitCodeArena;
}

namespace cerfengine::cerfir {

void BuildSoftTlbTarget(Engine& engine, JitCodeArena& arena, const GuestModel& model,
                        HostSoftTlb& tlb, HostGuestTarget& target, const char* owner);

}
