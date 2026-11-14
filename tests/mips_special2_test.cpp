#include "mips32_program_generator.h"

#include <array>
#include <bit>
#include <random>

namespace {

cerfengine::MipsFeatures Features(cerfengine::MipsIsaLevel isa = cerfengine::MipsIsaLevel::kMips32R1,
                                 bool tx39 = false) {
    cerfengine::MipsFeatures f;
    f.isa_level = isa;
    f.tx39_multiply_add = tx39;
    return f;
}

uint32_t Special2(uint32_t rs, uint32_t rt, uint32_t rd, uint32_t fn) {
    return (0x1Cu << 26) | MipsGenR(rs, rt, rd, 0u, fn);
}

void Finish(MipsIrRig& rig) {
    for (uint32_t n = 0; n < 64u && !rig.Finished(); ++n)
        TEST_CHECK(rig.Step() == cerfengine::GuestEngine::RunStatus::kRunning);
    TEST_CHECK(rig.Finished());
    TEST_CHECK(rig.Events().exceptions == 0u);
}

void Execute(MipsIrRig& rig, uint32_t word, const cerfengine::MipsCpuState& initial) {
    rig.Load(kMipsRigVaBase, {word, MipsGenJ(cerfengine::MipsOp::kJ, kMipsRigEndVa), 0u});
    rig.Begin(initial);
    Finish(rig);
}

cerfengine::MipsCpuState Initial(uint32_t a, uint32_t b, uint64_t acc) {
    cerfengine::MipsCpuState s{};
    s.pc = kMipsRigVaBase;
    s.gpr[1] = 0x1234567800000000ull | a;
    s.gpr[2] = 0x8765432100000000ull | b;
    s.gpr[3] = 0xABCDEFFFDEADBEEFull;
    s.hi = 0xA5A5A5A500000000ull | static_cast<uint32_t>(acc >> 32);
    s.lo = 0x5A5A5A5A00000000ull | static_cast<uint32_t>(acc);
    return s;
}

uint64_t Sext(uint32_t v) {
    return static_cast<uint64_t>(static_cast<int64_t>(std::bit_cast<int32_t>(v)));
}

void ProductsAndAccumulators(bool verify) {
    MipsIrRig rig(Features(), 12u, verify);
    std::vector<std::array<uint32_t, 2>> pairs = {
        {0u, 0u}, {1u, 1u}, {0xFFFFFFFFu, 2u}, {0x80000000u, 0xFFFFFFFFu},
        {0x80000000u, 0x80000000u}, {0x7FFFFFFFu, 0x7FFFFFFFu}, {0xFFFFFFFFu, 0xFFFFFFFFu},
    };
    std::mt19937 random(0x1300u);
    for (uint32_t n = 0; n < 128u; ++n)
        pairs.push_back({static_cast<uint32_t>(random()), static_cast<uint32_t>(random())});
    constexpr uint64_t kAccumulators[] = {
        0u, 0xFFFFFFFFFFFFFFFFull, 0x7FFFFFFFFFFFFFFFull,
        0x8000000000000000ull, 0x0123456789ABCDEFull,
    };
    for (const auto& pair : pairs) {
        const uint32_t a = pair[0];
        const uint32_t b = pair[1];
        for (uint64_t acc : kAccumulators) {
            for (uint32_t fn : {0u, 1u, 4u, 5u}) {
                const auto initial = Initial(a, b, acc);
                const uint64_t product = (fn & 1u) != 0u
                    ? static_cast<uint64_t>(a) * b
                    : static_cast<uint64_t>(static_cast<int64_t>(std::bit_cast<int32_t>(a)) *
                                             static_cast<int64_t>(std::bit_cast<int32_t>(b)));
                const uint64_t expected = fn < 4u ? acc + product : acc - product;
                Execute(rig, Special2(1u, 2u, 0u, fn), initial);
                TEST_CHECK(static_cast<uint32_t>(rig.State().lo) == static_cast<uint32_t>(expected));
                TEST_CHECK(static_cast<uint32_t>(rig.State().hi) == static_cast<uint32_t>(expected >> 32));
                TEST_CHECK(rig.State().gpr[1] == initial.gpr[1]);
                TEST_CHECK(rig.State().gpr[2] == initial.gpr[2]);
                TEST_CHECK(rig.State().gpr[3] == initial.gpr[3]);
                TEST_CHECK(rig.State().gpr[0] == 0u);
            }
        }
        const uint32_t expected = static_cast<uint32_t>(static_cast<uint64_t>(a) * b);
        for (uint32_t rd : {0u, 1u, 2u, 3u}) {
            Execute(rig, Special2(1u, 2u, rd, 2u), Initial(a, b, 0u));
            TEST_CHECK(rig.State().gpr[rd] == (rd == 0u ? 0u : Sext(expected)));
        }
    }
}

void LeadingBits(bool verify) {
    MipsIrRig rig(Features(), 12u, verify);
    std::vector<uint32_t> values = {0u, 0xFFFFFFFFu};
    for (uint32_t bit = 0u; bit < 32u; ++bit) {
        values.push_back(1u << bit);
        values.push_back(~(1u << bit));
    }
    for (uint32_t value : values) {
        for (uint32_t fn : {32u, 33u}) {
            const auto expected = static_cast<uint64_t>(fn == 32u
                ? std::countl_zero(value) : std::countl_one(value));
            for (uint32_t rd : {0u, 1u, 3u}) {
                Execute(rig, Special2(1u, rd, rd, fn), Initial(value, 0u, 0u));
                TEST_CHECK(rig.State().gpr[rd] == (rd == 0u ? 0u : expected));
            }
        }
    }
}

void ZeroSources(bool verify) {
    MipsIrRig rig(Features(), 12u, verify);
    for (uint32_t fn : {0u, 1u, 4u, 5u}) {
        for (uint32_t rs : {0u, 1u}) {
            Execute(rig, Special2(rs, 1u - rs, 0u, fn), Initial(0xFFFFFFFFu, 3u, 11u));
            TEST_CHECK(static_cast<uint32_t>(rig.State().lo) == 11u);
            TEST_CHECK(static_cast<uint32_t>(rig.State().hi) == 0u);
        }
    }
    for (uint32_t rs : {0u, 1u}) {
        Execute(rig, Special2(rs, 1u - rs, 3u, 2u), Initial(0xFFFFFFFFu, 3u, 11u));
        TEST_CHECK(rig.State().gpr[3] == 0u);
    }
    for (uint32_t fn : {32u, 33u}) {
        Execute(rig, Special2(0u, 3u, 3u, fn), Initial(0xFFFFFFFFu, 3u, 11u));
        TEST_CHECK(rig.State().gpr[3] == (fn == 32u ? 32u : 0u));
    }
}

void DelaySlotWrites(bool verify) {
    MipsIrRig rig(Features(), 12u, verify);
    for (uint32_t fn : {2u, 32u, 33u}) {
        for (uint32_t op : {cerfengine::MipsOp::kBEQ, cerfengine::MipsOp::kBEQL}) {
            for (uint32_t before : {0u, 5u}) {
                const bool taken = before == 0u;
                cerfengine::MipsCpuState initial{};
                initial.pc = kMipsRigVaBase;
                initial.gpr[1] = before;
                uint32_t result = 0u;
                if (fn == 2u) {
                    initial.gpr[2] = taken ? 1u : 0u;
                    initial.gpr[3] = 1u;
                    result = taken ? 1u : 0u;
                } else {
                    initial.gpr[2] = fn == 32u ? (taken ? 0u : 0x80000000u)
                                             : (taken ? 0xFFFFFFFFu : 0u);
                    result = taken ? 32u : 0u;
                }
                const uint32_t slot = Special2(2u, fn == 2u ? 3u : 1u, 1u, fn);
                rig.Load(kMipsRigVaBase, {
                    MipsGenI(op, 1u, 0u, 4u), slot,
                    MipsGenI(cerfengine::MipsOp::kADDIU, 0u, 10u, 1u),
                    MipsGenJ(cerfengine::MipsOp::kJ, kMipsRigEndVa), 0u,
                    MipsGenI(cerfengine::MipsOp::kADDIU, 0u, 11u, 1u),
                    MipsGenJ(cerfengine::MipsOp::kJ, kMipsRigEndVa), 0u,
                });
                rig.Begin(initial);
                Finish(rig);
                TEST_CHECK(rig.State().gpr[10] == (taken ? 0u : 1u));
                TEST_CHECK(rig.State().gpr[11] == (taken ? 1u : 0u));
                const bool nullified = op == cerfengine::MipsOp::kBEQL && !taken;
                TEST_CHECK(rig.State().gpr[1] == (nullified ? before : result));
            }
        }
    }
}

void RejectionAndLegacy(bool verify) {
    constexpr uint32_t kWords[] = {
        0x70220000u, 0x70220001u, 0x70220004u, 0x70220005u,
        0x70221802u, 0x70231820u, 0x70231821u,
    };
    for (auto isa : {cerfengine::MipsIsaLevel::kMips1, cerfengine::MipsIsaLevel::kMips3,
                     cerfengine::MipsIsaLevel::kMips4}) {
        MipsIrRig rig(Features(isa), 12u, verify);
        for (uint32_t word : kWords) {
            rig.Load(kMipsRigVaBase, {word});
            rig.Begin(Initial(3u, 7u, 0u));
            TEST_CHECK(rig.Step() == cerfengine::GuestEngine::RunStatus::kHalted);
        }
    }
    MipsIrRig rig(Features(), 12u, verify);
    for (uint32_t word : kWords) {
        rig.Load(kMipsRigVaBase, {word | (1u << 6)});
        rig.Begin(Initial(3u, 7u, 0u));
        TEST_CHECK(rig.Step() == cerfengine::GuestEngine::RunStatus::kHalted);
    }
    for (uint32_t fn : {0u, 1u, 4u, 5u, 32u, 33u}) {
        rig.Load(kMipsRigVaBase, {Special2(1u, 2u, 3u, fn)});
        rig.Begin(Initial(3u, 7u, 0u));
        TEST_CHECK(rig.Step() == cerfengine::GuestEngine::RunStatus::kHalted);
    }
    MipsIrRig tx39(Features(cerfengine::MipsIsaLevel::kMips1, true), 12u, verify);
    for (uint32_t fn : {0u, 1u}) {
        Execute(tx39, Special2(1u, 2u, 3u, fn), Initial(3u, 7u, 10u));
        TEST_CHECK(static_cast<uint32_t>(tx39.State().lo) == 31u);
        TEST_CHECK(static_cast<uint32_t>(tx39.State().hi) == 0u);
        TEST_CHECK(tx39.State().gpr[3] == 31u);
    }
}

}

int main() {
    TEST_CHECK(Special2(1u, 2u, 0u, 0u) == 0x70220000u);
    TEST_CHECK(Special2(1u, 2u, 3u, 2u) == 0x70221802u);
    TEST_CHECK(Special2(1u, 3u, 3u, 32u) == 0x70231820u);
    for (bool verify : {false, true}) {
        ProductsAndAccumulators(verify);
        LeadingBits(verify);
        ZeroSources(verify);
        DelaySlotWrites(verify);
        RejectionAndLegacy(verify);
    }
    std::puts("mips_special2_test passed");
}
