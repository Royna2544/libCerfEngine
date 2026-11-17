#include <cerfengine/isa_block_space.h>

#include <bit>

namespace cerfengine {

namespace {

constexpr uint32_t kPageBytes    = 0x1000u;
constexpr uint32_t kSlotsPerPage = kPageBytes / 4u;
constexpr uint32_t kWordsPerPage = kSlotsPerPage / 32u;
static_assert(kJumpCacheSize % kSlotsPerPage == 0u);

}

void IsaBlockSpace::JumpCacheClearRange(uint32_t base_va,
                                        uint32_t span_bytes) {
    if (span_bytes == kPageBytes) base_va &= 0xFFFFF000u;
    const auto matches = [&](uint32_t index) {
        const uint32_t page = JumpCacheLiveKey(jump_cache[index]) & 0xFFFFF000u;
        return page - base_va < span_bytes;
    };
    const uint32_t lead  = (0u - base_va) & (kPageBytes - 1u);
    const uint64_t pages = span_bytes > lead ? (static_cast<uint64_t>(span_bytes) - lead + kPageBytes - 1u) / kPageBytes
                                             : 0u;
    if (pages * kWordsPerPage >= jump_cache_filled_count || pages * kSlotsPerPage >= kJumpCacheSize) {
        for (uint32_t i = 0; i < jump_cache_filled_count; ++i) {
            const uint32_t index = jump_cache_filled[i];
            if (matches(index)) JumpCacheClearEntry(index);
        }
        return;
    }
    for (uint64_t k = 0; k < pages; ++k) {
        const uint32_t page  = base_va + lead + static_cast<uint32_t>(k) * kPageBytes;
        const uint32_t first = JumpCacheIndex(page) / 32u;
        for (uint32_t w = 0; w < kWordsPerPage; ++w) {
            uint32_t bits = jump_cache_filled_bits[first + w];
            while (bits != 0u) {
                const uint32_t index = (first + w) * 32u + static_cast<uint32_t>(std::countr_zero(bits));
                bits &= bits - 1u;
                if (matches(index)) JumpCacheClearEntry(index);
            }
        }
    }
}

}
