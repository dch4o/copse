// BDD tests for PointStore<Dim>: capacity, liveness, point read-back, and acquire semantics.

#include "topiary/impl/point_store.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <set>
#include <stdexcept>
#include <vector>

namespace topiary::internal {

using P3f = detail::PointType<3>;

SCENARIO("PointStore<3> construction", "[point_store][ctor]") {
    GIVEN("a positive capacity") {
        WHEN("the store is constructed") {
            PointStore<3> store{4};
            THEN("capacity matches and no index is live") {
                REQUIRE(store.capacity() == 4);
                REQUIRE(store.size() == 0);
                for (std::uint32_t i = 0; i < 4; ++i) {
                    REQUIRE_FALSE(store.is_live(i));
                }
            }
        }
    }

    GIVEN("zero capacity") {
        THEN("the constructor throws std::invalid_argument") {
            REQUIRE_THROWS_AS(PointStore<3>{0}, std::invalid_argument);
        }
    }
}

SCENARIO("PointStore<3>::acquire writes the point and returns sequential indices", "[point_store][acquire]") {
    GIVEN("an empty store with capacity 3") {
        PointStore<3> store{3};
        WHEN("three points are acquired in order") {
            const auto i0 = store.acquire(P3f{1, 2, 3});
            const auto i1 = store.acquire(P3f{4, 5, 6});
            const auto i2 = store.acquire(P3f{7, 8, 9});
            THEN("indices are 0, 1, 2 and the live count tracks them") {
                REQUIRE(i0 == 0);
                REQUIRE(i1 == 1);
                REQUIRE(i2 == 2);
                REQUIRE(store.size() == 3);
            }
            AND_THEN("each acquired index is live and reads back what was written") {
                REQUIRE(store.is_live(i0));
                REQUIRE(store.is_live(i1));
                REQUIRE(store.is_live(i2));
                REQUIRE((store.point(i0) - P3f{1, 2, 3}).norm() == 0.0f);
                REQUIRE((store.point(i1) - P3f{4, 5, 6}).norm() == 0.0f);
                REQUIRE((store.point(i2) - P3f{7, 8, 9}).norm() == 0.0f);
            }
        }
    }
}

SCENARIO("PointStore<3>::acquire wraps around when capacity is exceeded", "[point_store][acquire][fifo]") {
    GIVEN("a store of capacity 3 filled in FIFO order") {
        PointStore<3> store{3};
        const auto    i0 = store.acquire(P3f{0, 0, 0});
        (void)store.acquire(P3f{1, 1, 1});
        (void)store.acquire(P3f{2, 2, 2});

        WHEN("a fourth point is acquired") {
            const auto i_new = store.acquire(P3f{9, 9, 9});
            THEN("the returned index is the FIFO head and live count stays at capacity") {
                REQUIRE(i_new == i0);
                REQUIRE(store.size() == 3);
                REQUIRE(store.is_live(i_new));
                REQUIRE((store.point(i_new) - P3f{9, 9, 9}).norm() == 0.0f);
            }
        }
    }
}

SCENARIO("PointStore<3> acquire/release maintains the live set across mixed operations",
         "[point_store][acquire][release]") {
    GIVEN("a store of capacity 4 with three acquired points") {
        PointStore<3> store{4};
        const auto    i0 = store.acquire(P3f{0, 0, 0});
        const auto    i1 = store.acquire(P3f{1, 1, 1});
        const auto    i2 = store.acquire(P3f{2, 2, 2});

        WHEN("the middle index is released") {
            store.release(i1);

            THEN("only the surviving indices are live and the count drops") {
                REQUIRE(store.is_live(i0));
                REQUIRE(store.is_live(i2));
                REQUIRE_FALSE(store.is_live(i1));
                REQUIRE(store.size() == 2);
            }

            AND_WHEN("another point is acquired") {
                const auto i3 = store.acquire(P3f{3, 3, 3});
                THEN("the new index is live and reads back what was written") {
                    REQUIRE(store.is_live(i3));
                    REQUIRE(store.size() == 3);
                    REQUIRE((store.point(i3) - P3f{3, 3, 3}).norm() == 0.0f);
                }
                AND_THEN("for_each_live visits exactly the three live indices") {
                    std::set<std::uint32_t> visited;
                    store.for_each_live([&visited](std::uint32_t idx, const P3f&) { visited.insert(idx); });
                    REQUIRE(visited.size() == 3);
                    REQUIRE(visited.count(i0) == 1);
                    REQUIRE(visited.count(i2) == 1);
                    REQUIRE(visited.count(i3) == 1);
                }
            }
        }

        WHEN("the head is released, the tail is released, and the store is refilled past capacity") {
            store.release(i0);
            store.release(i2);
            REQUIRE(store.size() == 1);

            (void)store.acquire(P3f{10, 10, 10});
            (void)store.acquire(P3f{11, 11, 11});
            (void)store.acquire(P3f{12, 12, 12});
            (void)store.acquire(P3f{13, 13, 13});

            THEN("the store stays at capacity with exactly four live indices") {
                REQUIRE(store.size() == 4);
                std::size_t live_count = 0;
                store.for_each_live([&live_count](std::uint32_t, const P3f&) { ++live_count; });
                REQUIRE(live_count == 4);
            }
        }
    }
}

SCENARIO("PointStore<3>::generation bumps on every slot reuse", "[point_store][generation]") {
    GIVEN("a store with capacity 2") {
        PointStore<3> store{2};
        THEN("initial generation is zero for every index") {
            REQUIRE(store.generation(0) == 0);
            REQUIRE(store.generation(1) == 0);
        }

        WHEN("a slot is acquired (normal branch, store not full)") {
            const auto i0 = store.acquire(P3f{0, 0, 0});
            const auto gen_after_acquire = store.generation(i0);
            const auto i1 = store.acquire(P3f{1, 1, 1});

            THEN("the assigned slot's gen bumped from 0 to 1 on first acquire") {
                REQUIRE(gen_after_acquire == 1);
                REQUIRE(store.generation(i1) == 1);
            }

            AND_WHEN("the slot is released and re-acquired (normal branch reuse)") {
                store.release(i0);
                const auto gen_after_release = store.generation(i0);
                const auto i0_reacquired = store.acquire(P3f{9, 9, 9});

                THEN("release does NOT bump the gen but re-acquire does") {
                    REQUIRE(gen_after_release == 1);
                    REQUIRE(i0_reacquired == i0);
                    REQUIRE(store.generation(i0) == 2);
                }
            }
        }

        WHEN("the store is filled and a third acquire triggers FIFO eviction") {
            const auto i0 = store.acquire(P3f{0, 0, 0});
            (void)store.acquire(P3f{1, 1, 1});
            const auto gen_pre_eviction = store.generation(i0);

            const auto i_new = store.acquire(P3f{2, 2, 2});

            THEN("the FIFO victim's gen was bumped during eviction") {
                REQUIRE(i_new == i0);
                REQUIRE(gen_pre_eviction == 1);
                // Victim bump (eviction) + new-occupant bump (normal branch) = 2 bumps total.
                REQUIRE(store.generation(i0) == 3);
            }
        }
    }
}

SCENARIO("PointStore<3>::for_each_live visits every live entry exactly once", "[point_store][iteration]") {
    GIVEN("a store with three acquired points") {
        PointStore<3> store{4};
        (void)store.acquire(P3f{0, 0, 0});
        (void)store.acquire(P3f{1, 1, 1});
        (void)store.acquire(P3f{2, 2, 2});

        WHEN("for_each_live is invoked") {
            std::size_t visited = 0;
            store.for_each_live([&visited](std::uint32_t /*idx*/, const P3f& /*p*/) { ++visited; });
            THEN("the visit count equals the number of live entries") {
                REQUIRE(visited == 3);
            }
        }
    }
}

} // namespace topiary::internal
