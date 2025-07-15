#include <gtest/gtest.h>
#include "../utils/ArenaAllocator.hpp"
#include <vector>
#include <cstdint>
#include <thread>
#include <atomic>
#include <memory>

using namespace utils;

// Helper to check alignment
bool is_aligned(void* ptr, std::size_t alignment) {
    return reinterpret_cast<std::uintptr_t>(ptr) % alignment == 0;
}

TEST(ArenaAllocatorTest, AlignmentStress) {
    ArenaAllocator arena(4096);
    for (std::size_t align = 1; align <= 512; align *= 2) {
        void* p = arena.allocate(align, align);
        EXPECT_TRUE(is_aligned(p, align)) << "Failed at align = " << align;
    }
}

TEST(ArenaAllocatorTest, FragmentationPressure) {
    ArenaAllocator arena(4096);
    std::vector<void*> blocks;

    for (int i = 0; i < 1024; ++i) {
        void* p = arena.allocate(4);
        if (!p) break;  // graceful exit on exhaustion
        blocks.push_back(p);
    }

    // Validate allocator didn't crash and respected memory bounds
    EXPECT_LE(blocks.size(), 1024);
    EXPECT_GT(blocks.size(), 0);
}

TEST(ArenaAllocatorTest, ReuseAfterReset) {
    ArenaAllocator arena(4096);
    void* first = arena.allocate(64);
    arena.reset();
    void* second = arena.allocate(64);
    EXPECT_EQ(first, second);
}

TEST(ArenaAllocatorTest, FullCapacityAndExceed) {
    ArenaAllocator arena(128);
    EXPECT_NO_THROW(arena.allocate(120));
    EXPECT_THROW(arena.allocate(16), std::bad_alloc);
}

TEST(ArenaAllocatorTest, OveralignedStructAllocation) {
    struct alignas(64) Padded { char data[64]; };
    ArenaAllocator arena(8192);
    std::vector<Padded*> ptrs;
    for (int i = 0; i < 100; ++i) {
        auto* p = static_cast<Padded*>(arena.allocate(sizeof(Padded), alignof(Padded)));
        ASSERT_TRUE(is_aligned(p, alignof(Padded)));
        ptrs.push_back(p);
    }
}