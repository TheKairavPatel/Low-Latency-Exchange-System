#include "engine.hpp"
#include <cstdio>

static constexpr uint32_t BASE = 74000;

int main()
{
    Engine* e = new Engine(BASE);

    // seed the book
    e->processOrder({74185, 100, 1, 0});  // bid 100 @ 74185
    e->processOrder({74184, 100, 2, 0});  // bid 100 @ 74184
    e->processOrder({74183, 100, 3, 0});  // bid 100 @ 74183
    e->processOrder({74182, 100, 4, 0});  // bid 100 @ 74182
    e->processOrder({74181, 100, 5, 0});  // bid 100 @ 74181
    e->processOrder({74186, 100, 6,  1}); // ask 100 @ 74186
    e->processOrder({74187, 100, 7,  1}); // ask 100 @ 74187
    e->processOrder({74188, 100, 8,  1}); // ask 100 @ 74188
    e->processOrder({74189, 100, 9,  1}); // ask 100 @ 74189
    e->processOrder({74190, 100, 10, 1}); // ask 100 @ 74190
    printf("=== Book seeded (no events expected) ===\n\n");

    printf("=== Test 1: Limit buy crosses best ask exactly (100@74186) ===\n");
    // expect: Fill 100@74186 maker ID6, Fill 100@74186 taker ID11
    e->processOrder({74186, 100, 11, 0});
    printf("\n");

    printf("=== Test 2: Limit buy sweeps 3 ask levels (qty=280) ===\n");
    // asks left: 74187(100), 74188(100), 74189(100), 74190(100)
    // buy 280 @ 74190 — fills 74187(100), 74188(100), 74189(80 partial)
    // expect 6 events: maker+taker per level
    e->processOrder({74190, 280, 12, 0});
    printf("\n");

    printf("=== Test 3: Partial fill — taker smaller than maker ===\n");
    // best bid is 74185 (ID1, qty=100)
    // sell 40 @ 74185 — partial fill on ID1
    // expect: Fill 40@74185 maker ID1, Fill 40@74185 taker ID13
    e->processOrder({74185, 40, 13, 1});
    printf("\n");

    printf("=== Test 4: Cancel ID2 then sell through remaining bids ===\n");
    // cancel ID2 (bid 74184)
    e->processOrder({0, 0, 2, 2});
    // sell 100 @ 74183 — skips cancelled 74184, hits 74183 (ID3)
    // expect: Cancel ID2, Fill 100@74183 maker ID3, Fill 100@74183 taker ID14
    e->processOrder({74183, 100, 14, 1});
    printf("\n");

    printf("=== Test 5: Market buy sweeps remaining asks ===\n");
    // asks left: 74189(20 remaining from test2), 74190(100)
    // market buy 200 — fills 74189(20), 74190(100), 80 cancelled silently
    e->processOrder({0, 200, 15, 3});
    printf("\n");

    printf("=== Test 6: Market sell sweeps remaining bids ===\n");
    // bids left: 74185(60 from test3), 74182(100), 74181(100)
    // market sell 250 — fills 74185(60), 74182(100), 74181(90 partial)
    e->processOrder({0, 250, 16, 4});
    printf("\n");

    printf("=== Test 7: FIFO — 3 orders same level, market buy sweeps all ===\n");
    e->processOrder({74200, 50, 17, 1}); // ask 50 @ 74200
    e->processOrder({74200, 30, 18, 1}); // ask 30 @ 74200
    e->processOrder({74200, 20, 19, 1}); // ask 20 @ 74200
    // market buy 100 — fills ID17(50), ID18(30), ID19(20) in FIFO order
    e->processOrder({0, 100, 20, 3});
    printf("\n");

    printf("=== Test 8: Cancel already-filled order (no-op) ===\n");
    // ID17 was fully filled in test 7, cancel should be silent
    e->processOrder({0, 0, 17, 2});
    printf("  (no cancel event expected above)\n\n");

    delete e;
    return 0;
}