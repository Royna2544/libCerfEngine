#include "test_host.h"
#include "test_mips_system.h"

#include <cerfengine/guest_engine.h>
#include <cerfengine/mips/mips_bus_master_coherence.h>
#include <cerfengine/mips/mips_cpu.h>
#include <array>

int main() {
    TestHost host(0u, 0x10000u, 0x10000000u);
    TestMipsSystem system(0x80002FF4u);
    cerfengine::Setup setup;
    setup.isa = cerfengine::Isa::kMips;
    setup.mips.isa_level = cerfengine::MipsIsaLevel::kMips32R1;
    host.Attach(setup);
    system.Attach(setup);
    cerfengine::Engine engine(setup);
    std::string error;
    TEST_CHECK(engine.Start(error));
    engine.BindClock(host);
    auto& cpu = engine.Get<cerfengine::MipsCpu>();
    auto& guest = engine.Get<cerfengine::GuestEngine>();
    auto& coherence = engine.Get<cerfengine::MipsBusMasterCoherence>();
    cpu.ResetState();
    guest.SetHostChainExit(true);
    auto& state = *cpu.State();
    std::array<uint32_t, 6u> code = {0x2402000Bu, 0x08001400u, 0u,
                                    0x2402000Bu, 0x08001400u, 0u};
    host.LoadWords(0x2FF4u, code.data(), code.size());
    const auto execute = [&](uint32_t segment, uint32_t pa) {
        state.pc = segment | pa;
        state.branch_state = cerfengine::MipsBranch::kNone;
        state.gpr[2] = 0u;
        for (uint32_t step = 0u; step < 8u && state.pc != (segment | 0x5000u); ++step) {
            TEST_CHECK(guest.Run() == cerfengine::GuestEngine::RunStatus::kRunning);
        }
        TEST_CHECK(state.pc == (segment | 0x5000u));
        return static_cast<uint32_t>(state.gpr[2]);
    };
    for (uint32_t segment : {0x80000000u, 0xA0000000u}) {
        for (uint32_t pa : {0x2FF4u, 0x3000u}) TEST_CHECK(execute(segment, pa) == 11u);
    }
    code[0] = 0x24020016u;
    code[3] = 0x24020021u;
    host.LoadWords(0x2FF4u, code.data(), code.size());
    coherence.AfterWrite(0x2FF4u, static_cast<uint32_t>(code.size() * sizeof(uint32_t)));
    for (uint32_t segment : {0x80000000u, 0xA0000000u}) {
        TEST_CHECK(execute(segment, 0x2FF4u) == 22u);
        TEST_CHECK(execute(segment, 0x3000u) == 33u);
    }
    std::puts("mips_bus_master_coherence_test passed");
}
