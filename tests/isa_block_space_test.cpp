#include "test_host.h"

#include <cerfengine/isa_block_space.h>

#include <array>
#include <chrono>
#include <memory>

namespace {

using Clock = std::chrono::steady_clock;

std::unique_ptr<cerfengine::IsaBlockSpace> NewSpace() {
    auto space = std::make_unique<cerfengine::IsaBlockSpace>();
    space->JumpCacheFlush();
    return space;
}

void Insert(cerfengine::IsaBlockSpace& space, uint32_t va) {
    space.JumpCacheInsert(cerfengine::kBlockPrivileged, va,
                          reinterpret_cast<void*>(static_cast<uintptr_t>(1u)), nullptr);
}

void TestSelectiveClear() {
    auto space = NewSpace();
    constexpr uint32_t target = 0x12345000u;
    constexpr uint32_t other = 0x12349000u;
    Insert(*space, target + 4u);
    Insert(*space, other + 8u);
    space->JumpCacheClearPage(target + 0x3Cu);
    TEST_CHECK(space->JumpCacheLookup(cerfengine::kBlockPrivileged, target + 4u) == nullptr);
    TEST_CHECK(space->JumpCacheLookup(cerfengine::kBlockPrivileged, other + 8u) != nullptr);
    Insert(*space, target + 4u);
    space->JumpCacheClearRange(target, 0x4000u);
    TEST_CHECK(space->JumpCacheLookup(cerfengine::kBlockPrivileged, target + 4u) == nullptr);
    TEST_CHECK(space->JumpCacheLookup(cerfengine::kBlockPrivileged, other + 8u) != nullptr);
    space->JumpCacheFlush();
    TEST_CHECK(space->JumpCacheLookup(cerfengine::kBlockPrivileged, other + 8u) == nullptr);
}

void TestAliasAndReinsert() {
    auto space = NewSpace();
    constexpr uint32_t first = 0x10402000u;
    constexpr uint32_t alias = first + (cerfengine::kJumpCacheSize << 2);
    Insert(*space, first);
    Insert(*space, alias);
    space->JumpCacheClearPage(first);
    TEST_CHECK(space->JumpCacheLookup(cerfengine::kBlockPrivileged, alias) != nullptr);
    space->JumpCacheClearPage(alias);
    TEST_CHECK(space->JumpCacheLookup(cerfengine::kBlockPrivileged, alias) == nullptr);
    Insert(*space, first);
    space->JumpCacheClearRange(first, 0x2000u);
    TEST_CHECK(space->JumpCacheLookup(cerfengine::kBlockPrivileged, first) == nullptr);
}

void TestWrappedRange() {
    auto space = NewSpace();
    Insert(*space, 0xFFFFF000u);
    Insert(*space, 0x00000004u);
    Insert(*space, 0x00001008u);
    space->JumpCacheClearRange(0xFFFFF000u, 0x2000u);
    TEST_CHECK(space->JumpCacheLookup(cerfengine::kBlockPrivileged, 0xFFFFF000u) == nullptr);
    TEST_CHECK(space->JumpCacheLookup(cerfengine::kBlockPrivileged, 0x00000004u) == nullptr);
    TEST_CHECK(space->JumpCacheLookup(cerfengine::kBlockPrivileged, 0x00001008u) != nullptr);
}

void TestRangeClearMatchesReference() {
    auto space = NewSpace();
    std::array<uint32_t, cerfengine::kJumpCacheSize> model{};
    model.fill(cerfengine::kJumpCacheNoKey);
    uint32_t seed = 0x2545F491u;
    const auto next = [&seed] {
        seed ^= seed << 13;
        seed ^= seed >> 17;
        seed ^= seed << 5;
        return seed;
    };
    for (uint32_t round = 0; round < 20000u; ++round) {
        const uint32_t inserts = next() % 48u;
        for (uint32_t i = 0; i < inserts; ++i) {
            const uint32_t va = (next() & 0x0003FFFCu) | ((next() & 3u) == 0u ? 0xFFFC0000u : 0u);
            Insert(*space, va);
            model[cerfengine::IsaBlockSpace::JumpCacheIndex(va)] = va;
        }
        uint32_t base = next();
        if ((next() & 1u) == 0u) base &= 0xFFFFF000u;
        if ((next() & 3u) == 0u) base = 0xFFFFF000u - (next() & 0x3000u);
        const uint32_t spans[] = {0x1000u, 0x2000u, 0x4000u, 1u + (next() & 0x7FFFu)};
        const uint32_t span = spans[next() & 3u];
        space->JumpCacheClearRange(base, span);
        const uint32_t clear_base = span == 0x1000u ? (base & 0xFFFFF000u) : base;
        for (uint32_t& key : model) {
            if (key != cerfengine::kJumpCacheNoKey && (key & 0xFFFFF000u) - clear_base < span) {
                key = cerfengine::kJumpCacheNoKey;
            }
        }
        for (uint32_t index = 0; index < cerfengine::kJumpCacheSize; ++index) {
            const uint32_t key = model[index];
            if (key == cerfengine::kJumpCacheNoKey) continue;
            TEST_CHECK(space->JumpCacheLookup(cerfengine::kBlockPrivileged, key) != nullptr);
        }
        uint32_t live = 0;
        for (uint32_t index = 0; index < cerfengine::kJumpCacheSize; ++index) {
            if (space->jump_cache[index].native != nullptr) ++live;
        }
        uint32_t expected = 0;
        for (uint32_t key : model) expected += key != cerfengine::kJumpCacheNoKey ? 1u : 0u;
        TEST_CHECK(live == expected);
        if ((next() & 255u) == 0u) {
            space->JumpCacheFlush();
            model.fill(cerfengine::kJumpCacheNoKey);
        }
    }
}

uint64_t TimeClear(bool range, uint32_t filled) {
    auto space = NewSpace();
    for (uint32_t i = 0; i < filled; ++i) Insert(*space, 0x20000000u + i * 4u);
    const auto begin = Clock::now();
    for (uint32_t i = 0; i < 20000u; ++i) {
        if (range) space->JumpCacheClearRange(0x10000000u, 0x2000u);
        else space->JumpCacheClearPage(0x10000000u);
    }
    const auto elapsed = std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - begin).count();
    TEST_CHECK(space->JumpCacheLookup(cerfengine::kBlockPrivileged, 0x20000000u) != nullptr);
    return static_cast<uint64_t>(elapsed);
}

void TestSparseCostTracksFilledSlots(bool range) {
    const uint64_t sparse = TimeClear(range, 1u);
    const uint64_t dense = TimeClear(range, 256u);
    std::printf("%s clear: sparse=%llu ns dense=%llu ns\n", range ? "range" : "page",
                static_cast<unsigned long long>(sparse), static_cast<unsigned long long>(dense));
    TEST_CHECK(dense > sparse * 2u);
}

}

int main() {
    TestSelectiveClear();
    TestAliasAndReinsert();
    TestWrappedRange();
    TestRangeClearMatchesReference();
    TestSparseCostTracksFilledSlots(false);
    TestSparseCostTracksFilledSlots(true);
    std::puts("isa_block_space_test passed");
}
