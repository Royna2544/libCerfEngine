#include "mips32_program_generator.h"

#include <cerfengine/engine_setup.h>

namespace {

cerfengine::MipsFeatures Features() {
    cerfengine::MipsFeatures f;
    f.isa_level = cerfengine::MipsIsaLevel::kMips32R1;
    return f;
}

void Finish(MipsIrRig& rig) {
    for (uint32_t n = 0; n < 64u && !rig.Finished(); ++n)
        TEST_CHECK(rig.Step() == cerfengine::GuestEngine::RunStatus::kRunning);
    TEST_CHECK(rig.Finished());
}

void WordComparisonsAndMoves(bool verify) {
    namespace S = cerfengine::MipsSpecial;
    MipsIrRig rig(Features(), 12u, verify);
    rig.Load(kMipsRigVaBase, {
        MipsGenR(1u, 2u, 3u, 0u, S::kSLTU),
        MipsGenR(1u, 2u, 4u, 0u, S::kSLT),
        MipsGenR(5u, 6u, 7u, 0u, S::kMOVZ),
        MipsGenR(5u, 6u, 8u, 0u, S::kMOVN),
        MipsGenR(5u, 2u, 9u, 0u, S::kMOVN),
        MipsGenJ(cerfengine::MipsOp::kJ, kMipsRigEndVa),
        0u,
    });
    cerfengine::MipsCpuState initial{};
    initial.pc = kMipsRigVaBase;
    initial.gpr[1] = 0x0000000080000000ull;
    initial.gpr[2] = 0xFFFFFFFF00000001ull;
    initial.gpr[5] = 0x1234567881234567ull;
    initial.gpr[6] = 0xFEDCBA9800000000ull;
    initial.gpr[8] = 27u;
    rig.Begin(initial);
    Finish(rig);
    TEST_CHECK(rig.State().gpr[3] == 0u);
    TEST_CHECK(rig.State().gpr[4] == 1u);
    TEST_CHECK(static_cast<uint32_t>(rig.State().gpr[7]) == 0x81234567u);
    TEST_CHECK(rig.State().gpr[8] == 27u);
    TEST_CHECK(static_cast<uint32_t>(rig.State().gpr[9]) == 0x81234567u);
}

void ExceptionReturn() {
    MipsIrRig rig(Features(), 12u, false);
    rig.Load(kMipsRigVaBase, {0x42000018u});
    cerfengine::MipsCpuState initial{};
    initial.pc = kMipsRigVaBase;
    rig.Begin(initial);
    Finish(rig);
    TEST_CHECK(rig.Events().erets == 1u);
}

void RejectDoubleword() {
    MipsIrRig rig(Features(), 12u, false);
    rig.Load(kMipsRigVaBase, {MipsGenR(1u, 2u, 3u, 0u, cerfengine::MipsSpecial::kDADDU)});
    cerfengine::MipsCpuState initial{};
    initial.pc = kMipsRigVaBase;
    rig.Begin(initial);
    TEST_CHECK(rig.Step() == cerfengine::GuestEngine::RunStatus::kHalted);
    TEST_CHECK(rig.State().gpr[3] == 0u);
}

}

int main() {
    TEST_CHECK(!cerfengine::MipsHas64BitGprs(cerfengine::MipsIsaLevel::kMips32R1));
    TEST_CHECK(cerfengine::MipsHas64BitGprs(cerfengine::MipsIsaLevel::kMips3));
    TEST_CHECK(cerfengine::MipsHas64BitGprs(cerfengine::MipsIsaLevel::kMips4));
    WordComparisonsAndMoves(false);
    WordComparisonsAndMoves(true);
    ExceptionReturn();
    RejectDoubleword();
    std::puts("mips32_profile_test passed");
}
