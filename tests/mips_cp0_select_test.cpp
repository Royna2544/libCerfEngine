#include "test_mips_system.h"

#include <cerfengine/guest_engine.h>
#include <cerfengine/mips/mips_cpu.h>

#include <cstddef>
#include <vector>

namespace {

constexpr uint32_t kRam = 0x1FC00000u;
constexpr uint32_t kEntry = 0xBFC00000u;
constexpr uint32_t kExit = kEntry + 0x100u;

uint32_t Move(uint32_t rs, uint32_t rt, uint32_t rd, uint32_t sel) {
    return (0x10u << 26) | (rs << 21) | (rt << 16) | (rd << 11) | sel;
}

class LegacySystem : public TestMipsSystem {
public:
    LegacySystem() : TestMipsSystem(kEntry) {}

    cerfengine::MipsCp0Decision MipsCp0(uint32_t rd) override {
        cerfengine::MipsCp0Decision d;
        if (rd != 12u && rd != 16u) return d;
        d.present = true;
        d.writable = true;
        d.offset = static_cast<int32_t>(rd == 12u
            ? offsetof(cerfengine::MipsCpuState, cp0_status)
            : offsetof(cerfengine::MipsCpuState, cp0_config));
        return d;
    }
};

class BankSystem : public LegacySystem {
public:
    using LegacySystem::MipsCp0;

    cerfengine::MipsCp0Decision MipsCp0(uint32_t rd, uint32_t sel) override {
        if (sel == 0u) return LegacySystem::MipsCp0(rd);
        cerfengine::MipsCp0Decision d;
        if (sel != 1u || (rd != 12u && rd != 16u)) return d;
        d.present = true;
        d.read = &Read;
        d.read_context = rd == 12u ? &control_ : &config_;
        if (rd == 12u) {
            d.writable = true;
            d.write = &Write;
            d.write_context = &control_;
        }
        return d;
    }

    uint32_t config_ = 0xA5A55A5Au;
    uint32_t control_ = 0u;

private:
    static uint32_t CERFENGINE_FASTCALL Read(void* context) {
        return *static_cast<uint32_t*>(context);
    }
    static void CERFENGINE_FASTCALL Write(void* context, uint32_t value) {
        *static_cast<uint32_t*>(context) = value;
    }
};

void Run(LegacySystem& system, cerfengine::MipsIsaLevel isa, bool verify,
         const std::vector<uint32_t>& program, bool halted, bool check_banks) {
    TestHost host(kRam, 0x1000u, 0x10000000u);
    host.LoadWords(kRam, program.data(), program.size());
    cerfengine::Setup setup;
    setup.isa = cerfengine::Isa::kMips;
    setup.mips.isa_level = isa;
    setup.verify_ir = verify;
    host.Attach(setup);
    system.Attach(setup);
    cerfengine::Engine engine(setup);
    std::string error;
    if (!engine.Start(error)) TestFail("Start: %s", error.c_str());
    engine.BindClock(host);
    auto& cpu = engine.Get<cerfengine::MipsCpu>();
    auto& guest = engine.Get<cerfengine::GuestEngine>();
    guest.SetHostChainExit(true);
    cpu.ResetState();
    auto& state = *cpu.State();
    state.cp0_config = 0x80000003u;
    state.gpr[1] = 4u;
    bool did_halt = false;
    for (uint32_t n = 0u; n < 32u && state.pc != kExit; ++n) {
        if (guest.Run() == cerfengine::GuestEngine::RunStatus::kHalted) {
            did_halt = true;
            break;
        }
    }
    TEST_CHECK(did_halt == halted);
    TEST_CHECK(state.cp0_status == 0u);
    TEST_CHECK(state.cp0_config == 0x80000003u);
    if (halted) {
        TEST_CHECK(state.gpr[2] == 0u);
        return;
    }
    TEST_CHECK(state.pc == kExit);
    if (check_banks) {
        TEST_CHECK(state.gpr[2] == 0xFFFFFFFFA5A55A5Aull);
        TEST_CHECK(state.gpr[3] == 4u);
        TEST_CHECK(state.gpr[4] == 0xFFFFFFFF80000003ull);
        TEST_CHECK(state.gpr[5] == 0u);
    }
}

}

int main() {
    const uint32_t jump = (2u << 26) | ((kExit >> 2) & 0x03FFFFFFu);
    const std::vector<uint32_t> banks = {
        Move(0u, 2u, 16u, 1u),
        Move(4u, 1u, 12u, 1u),
        Move(0u, 3u, 12u, 1u),
        Move(0u, 4u, 16u, 0u),
        Move(0u, 5u, 12u, 0u),
        jump, 0u,
    };
    for (bool verify : {false, true}) {
        BankSystem system;
        Run(system, cerfengine::MipsIsaLevel::kMips32R1, verify, banks, false, true);
        TEST_CHECK(system.control_ == 4u);
        Run(system, cerfengine::MipsIsaLevel::kMips32R1, verify,
            {Move(0u, 2u, 16u, 2u)}, true, false);
        LegacySystem legacy;
        Run(legacy, cerfengine::MipsIsaLevel::kMips32R1, verify,
            {Move(0u, 2u, 16u, 1u)}, true, false);
        Run(legacy, cerfengine::MipsIsaLevel::kMips32R1, verify,
            {Move(0u, 4u, 16u, 0u), jump, 0u}, false, false);
        for (auto isa : {cerfengine::MipsIsaLevel::kMips1, cerfengine::MipsIsaLevel::kMips3,
                         cerfengine::MipsIsaLevel::kMips4}) {
            Run(system, isa, verify, {Move(0u, 2u, 16u, 1u)}, true, false);
            Run(system, isa, verify, {Move(4u, 1u, 12u, 1u)}, true, false);
        }
    }
    std::puts("mips_cp0_select_test passed");
}
