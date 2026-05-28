#include <cstdio>
#include <cstdint>
#include <x86intrin.h>
#include <vector>
#include <algorithm>
#include <random>
#include <windows.h>
#include "engine.hpp"

static Engine engine(74000);

void drainAndPrint() {
    Event e;
    bool any = false;
    while (engine.outboundQueue.pop(e)) {
        any = true;
        printf("  [EVENT] type=%s orderID=%u price=%u qty=%u side=%s fullyFilled=%d\n",
            e.type == 0 ? "CANCEL" : "FILL",
            e.orderID, e.price, e.quantity,
            e.side == 0 ? "BUY" : "SELL",
            e.fullyFilled);
    }
    if (!any) printf("  [no events]\n");
}

int main() {
    // ── TEST 1: simple full fill ──
    printf("\n=== TEST 1: simple full fill ===\n");
    printf("place resting sell 100 @ 74100 (id=1)\n");
    engine.processOrder({74100, 100, 1, 1});
    drainAndPrint();

    printf("place matching buy 100 @ 74100 (id=2)\n");
    engine.processOrder({74100, 100, 2, 0});
    drainAndPrint();
    // expect: FILL id=1 sell fullyFilled=1, FILL id=2 buy fullyFilled=1

    // ── TEST 2: partial fill ──
    printf("\n=== TEST 2: partial fill ===\n");
    printf("place resting sell 50 @ 74200 (id=3)\n");
    engine.processOrder({74200, 50, 3, 1});
    drainAndPrint();

    printf("place aggressive buy 100 @ 74200 (id=4)\n");
    engine.processOrder({74200, 100, 4, 0});
    drainAndPrint();
    // expect: FILL id=3 sell fullyFilled=1, FILL id=4 buy fullyFilled=0 (50 remaining rests)

    // ── TEST 3: cancel ──
    printf("\n=== TEST 3: cancel ===\n");
    printf("place resting buy 100 @ 74050 (id=5)\n");
    engine.processOrder({74050, 100, 5, 0});
    drainAndPrint();

    printf("cancel id=5\n");
    engine.processOrder({0, 0, 5, 2});
    drainAndPrint();
    // expect: CANCEL id=5 buy

    // ── TEST 4: market buy ──
    printf("\n=== TEST 4: market buy ===\n");
    printf("place resting sell 100 @ 74300 (id=6)\n");
    engine.processOrder({74300, 100, 6, 1});
    drainAndPrint();

    printf("market buy 100 (id=7)\n");
    engine.processOrder({0, 100, 7, 3});
    drainAndPrint();
    // expect: FILL id=6 sell fullyFilled=1, FILL id=7 buy fullyFilled=1

    // ── TEST 5: market sell ──
    printf("\n=== TEST 5: market sell ===\n");
    printf("place resting buy 100 @ 74400 (id=8)\n");
    engine.processOrder({74400, 100, 8, 0});
    drainAndPrint();

    printf("market sell 100 (id=9)\n");
    engine.processOrder({0, 100, 9, 4});
    drainAndPrint();
    // expect: FILL id=8 buy fullyFilled=1, FILL id=9 sell fullyFilled=1

    // ── TEST 6: multi level fill ──
    printf("\n=== TEST 6: multi level aggressive buy sweeps two levels ===\n");
    printf("place resting sell 50 @ 74500 (id=10)\n");
    engine.processOrder({74500, 50, 10, 1});
    printf("place resting sell 50 @ 74501 (id=11)\n");
    engine.processOrder({74501, 50, 11, 1});
    drainAndPrint();

    printf("aggressive buy 100 @ 74502 (id=12)\n");
    engine.processOrder({74502, 100, 12, 0});
    drainAndPrint();
    // expect: FILL id=10 fullyFilled=1, FILL id=12 partial, FILL id=11 fullyFilled=1, FILL id=12 fullyFilled=1

    return 0;
}