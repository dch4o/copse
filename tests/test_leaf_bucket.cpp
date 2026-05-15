// BDD tests for LeafBucket: allocate offsets and view slices into the backing buffer.

#include "pkd_tree/impl/leaf_bucket.hpp"

#include <catch2/catch_test_macros.hpp>

namespace pkd_tree::internal {

SCENARIO("LeafBucket::allocate returns monotonically increasing offsets", "[leaf_bucket][allocate]") {
    GIVEN("an empty LeafBucket with reserved storage for 16 entries") {
        LeafBucket pool{16};
        WHEN("three slices are allocated of sizes 4, 8, 4") {
            const auto first_offset  = pool.allocate(4);
            const auto second_offset = pool.allocate(8);
            const auto third_offset  = pool.allocate(4);
            THEN("offsets are 0, 4, 12") {
                REQUIRE(first_offset == 0);
                REQUIRE(second_offset == 4);
                REQUIRE(third_offset == 12);
            }
        }
    }
}

SCENARIO("LeafBucket::view yields a span of the requested length over the backing buffer",
         "[leaf_bucket][view]") {
    GIVEN("a freshly allocated 8-entry slice") {
        LeafBucket pool{16};
        const auto offset = pool.allocate(8);
        WHEN("the mutable view is written through and the const view is read back") {
            auto slice = pool.view(offset, 8);
            for (std::size_t i = 0; i < slice.size(); ++i) {
                slice[i] = BucketEntry{static_cast<std::uint32_t>(i + 1),
                                       static_cast<std::uint32_t>((i + 1) * 10)};
            }
            const auto& cpool = pool;
            const auto  ro    = cpool.view(offset, 8);
            THEN("the read-back values match what was written") {
                REQUIRE(ro.size() == 8);
                for (std::size_t i = 0; i < ro.size(); ++i) {
                    REQUIRE(ro[i].index == i + 1);
                    REQUIRE(ro[i].gen == (i + 1) * 10);
                }
            }
        }
    }
}

SCENARIO("LeafBucket::push appends entries until capacity is reached", "[leaf_bucket][push]") {
    GIVEN("a 4-entry slice with size = 0") {
        LeafBucket          pool{16};
        const auto          offset   = pool.allocate(4);
        const std::uint16_t capacity = 4;
        std::uint16_t       size     = 0;
        WHEN("four entries are pushed in order") {
            const BucketEntry expected[] = {{10, 1}, {20, 2}, {30, 3}, {40, 4}};
            bool              results[4]{};
            for (std::size_t i = 0; i < 4; ++i) {
                results[i] = pool.push(offset, size, capacity, expected[i]);
            }
            THEN("each push returns true and size advances to capacity") {
                for (bool r : results) REQUIRE(r);
                REQUIRE(size == capacity);
            }
            THEN("the slice reads back the inserted (index, gen) tuples in order") {
                const auto& cpool = pool;
                const auto  ro    = cpool.view(offset, size);
                REQUIRE(ro.size() == 4);
                for (std::size_t i = 0; i < ro.size(); ++i) {
                    REQUIRE(ro[i].index == expected[i].index);
                    REQUIRE(ro[i].gen == expected[i].gen);
                }
            }
        }
    }
}

SCENARIO("LeafBucket::push returns false at capacity without mutating state",
         "[leaf_bucket][push][overflow]") {
    GIVEN("a 2-entry slice filled to capacity") {
        LeafBucket          pool{8};
        const auto          offset   = pool.allocate(2);
        const std::uint16_t capacity = 2;
        std::uint16_t       size     = 0;
        REQUIRE(pool.push(offset, size, capacity, BucketEntry{7, 1}));
        REQUIRE(pool.push(offset, size, capacity, BucketEntry{9, 2}));
        REQUIRE(size == capacity);
        WHEN("a third push is attempted") {
            const bool result = pool.push(offset, size, capacity, BucketEntry{99, 99});
            THEN("the call returns false") {
                REQUIRE_FALSE(result);
            }
            THEN("size is unchanged") {
                REQUIRE(size == capacity);
            }
            THEN("the slice contents are unchanged") {
                const auto& cpool = pool;
                const auto  ro    = cpool.view(offset, size);
                REQUIRE(ro.size() == 2);
                REQUIRE(ro[0].index == 7);
                REQUIRE(ro[0].gen == 1);
                REQUIRE(ro[1].index == 9);
                REQUIRE(ro[1].gen == 2);
            }
        }
    }
}

SCENARIO("LeafBucket::push into independent slices does not cross-contaminate",
         "[leaf_bucket][push][isolation]") {
    GIVEN("two separately allocated slices in the same pool") {
        LeafBucket          pool{16};
        const auto          first_offset  = pool.allocate(3);
        const auto          second_offset = pool.allocate(3);
        const std::uint16_t capacity      = 3;
        std::uint16_t       first_size    = 0;
        std::uint16_t       second_size   = 0;
        WHEN("entries are pushed into both slices interleaved") {
            REQUIRE(pool.push(first_offset, first_size, capacity, BucketEntry{1, 11}));
            REQUIRE(pool.push(second_offset, second_size, capacity, BucketEntry{100, 1100}));
            REQUIRE(pool.push(first_offset, first_size, capacity, BucketEntry{2, 22}));
            REQUIRE(pool.push(second_offset, second_size, capacity, BucketEntry{200, 2200}));
            REQUIRE(pool.push(first_offset, first_size, capacity, BucketEntry{3, 33}));
            REQUIRE(pool.push(second_offset, second_size, capacity, BucketEntry{300, 3300}));
            THEN("each slice holds only its own entries in push order") {
                const auto& cpool       = pool;
                const auto  first_view  = cpool.view(first_offset, first_size);
                const auto  second_view = cpool.view(second_offset, second_size);
                REQUIRE(first_view.size() == 3);
                REQUIRE(first_view[0].index == 1);
                REQUIRE(first_view[0].gen == 11);
                REQUIRE(first_view[1].index == 2);
                REQUIRE(first_view[1].gen == 22);
                REQUIRE(first_view[2].index == 3);
                REQUIRE(first_view[2].gen == 33);
                REQUIRE(second_view.size() == 3);
                REQUIRE(second_view[0].index == 100);
                REQUIRE(second_view[0].gen == 1100);
                REQUIRE(second_view[1].index == 200);
                REQUIRE(second_view[1].gen == 2200);
                REQUIRE(second_view[2].index == 300);
                REQUIRE(second_view[2].gen == 3300);
            }
        }
    }
}

} // namespace pkd_tree::internal
