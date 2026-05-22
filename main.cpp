#include <cstdio>
#include <cassert>
#include "priceLevel.hpp"

int main()
{
    // ─── TEST 1: Cancel head ───
    {
        PriceLevel level;
        ClientOrder o1 = {10000, 100, 1, 0};
        ClientOrder o2 = {10000, 200, 2, 0};
        ClientOrder o3 = {10000, 300, 3, 0};
        uint8_t s1 = level.insertOrder(o1);
        uint8_t s2 = level.insertOrder(o2);
        uint8_t s3 = level.insertOrder(o3);
        assert(s1 == 0 && s2 == 1 && s3 == 2);
        level.cancelOrder(s1);
        assert(level.getHead() == s2);
        assert(level.getTail() == s3);
        assert(!level.isEmpty());
        printf("[PASS] Cancel head\n");
    }

    // ─── TEST 2: Cancel tail ───
    {
        PriceLevel level;
        ClientOrder o1 = {10000, 100, 1, 0};
        ClientOrder o2 = {10000, 200, 2, 0};
        ClientOrder o3 = {10000, 300, 3, 0};
        uint8_t s1 = level.insertOrder(o1);
        uint8_t s2 = level.insertOrder(o2);
        uint8_t s3 = level.insertOrder(o3);
        assert(s1 == 0 && s2 == 1 && s3 == 2);
        level.cancelOrder(s3);
        assert(level.getHead() == s1);
        assert(level.getTail() == s2);
        assert(!level.isEmpty());
        printf("[PASS] Cancel tail\n");
    }

    // ─── TEST 3: Cancel middle ───
    {
        PriceLevel level;
        ClientOrder o1 = {10000, 100, 1, 0};
        ClientOrder o2 = {10000, 200, 2, 0};
        ClientOrder o3 = {10000, 300, 3, 0};
        uint8_t s1 = level.insertOrder(o1);
        uint8_t s2 = level.insertOrder(o2);
        uint8_t s3 = level.insertOrder(o3);
        assert(s1 == 0 && s2 == 1 && s3 == 2);
        level.cancelOrder(s2);
        assert(level.getHead() == s1);
        assert(level.getTail() == s3);
        ClientOrder o4 = {10000, 50, 4, 0};
        uint8_t s4 = level.insertOrder(o4);
        assert(s4 == s2);
        ClientOrder taker = {10000, 450, 9, 0};
        ClientOrder result = level.fillOrder(taker);
        assert(result.quantity == 0);
        assert(level.isEmpty());
        printf("[PASS] Cancel middle\n");
    }

    // ─── TEST 4: Fill exactly drains level ───
    {
        PriceLevel level;
        ClientOrder o1 = {10000, 100, 1, 0};
        ClientOrder o2 = {10000, 200, 2, 0};
        uint8_t s1 = level.insertOrder(o1);
        uint8_t s2 = level.insertOrder(o2);
        assert(s1 == 0 && s2 == 1);
        ClientOrder taker = {10000, 300, 9, 0};
        ClientOrder result = level.fillOrder(taker);
        assert(result.quantity == 0);
        assert(level.isEmpty());
        printf("[PASS] Fill exactly drains level\n");
    }

    // ─── TEST 5: Fill more than available ───
    {
        PriceLevel level;
        ClientOrder o1 = {10000, 100, 1, 0};
        ClientOrder o2 = {10000, 200, 2, 0};
        uint8_t s1 = level.insertOrder(o1);
        uint8_t s2 = level.insertOrder(o2);
        assert(s1 == 0 && s2 == 1);
        ClientOrder taker = {10000, 500, 9, 0};
        ClientOrder result = level.fillOrder(taker);
        assert(result.quantity == 200);
        assert(level.isEmpty());
        printf("[PASS] Fill more than available\n");
    }

    // ─── TEST 6: Fill partial, maker survives with correct qty ───
    {
        PriceLevel level;
        ClientOrder o1 = {10000, 500, 1, 0};
        uint8_t s1 = level.insertOrder(o1);
        assert(s1 == 0);
        ClientOrder taker = {10000, 200, 9, 0};
        ClientOrder result = level.fillOrder(taker);
        assert(result.quantity == 0);
        assert(!level.isEmpty());
        assert(level.getHead() == s1);
        assert(level.getTail() == s1);
        ClientOrder taker2 = {10000, 300, 10, 0};
        ClientOrder result2 = level.fillOrder(taker2);
        assert(result2.quantity == 0);
        assert(level.isEmpty());
        printf("[PASS] Fill partial, maker survives with correct qty\n");
    }

    // ─── TEST 7: Slot reuse after full drain via fill ───
    {
        PriceLevel level;
        ClientOrder o1 = {10000, 100, 1, 0};
        ClientOrder o2 = {10000, 200, 2, 0};
        uint8_t s1 = level.insertOrder(o1);
        uint8_t s2 = level.insertOrder(o2);
        assert(s1 == 0 && s2 == 1);
        ClientOrder taker = {10000, 300, 9, 0};
        level.fillOrder(taker);
        assert(level.isEmpty());
        ClientOrder o3 = {10000, 50, 3, 0};
        ClientOrder o4 = {10000, 50, 4, 0};
        uint8_t s3 = level.insertOrder(o3);
        uint8_t s4 = level.insertOrder(o4);
        assert(s3 == 1);
        assert(s4 == 0);
        assert(level.getHead() == s3);
        assert(level.getTail() == s4);
        printf("[PASS] Slot reuse after full drain\n");
    }

    // ─── TEST 8: Cancel then reinsert, same slot recycled ───
    {
        PriceLevel level;
        ClientOrder o1 = {10000, 100, 1, 0};
        uint8_t s1 = level.insertOrder(o1);
        assert(s1 == 0);
        level.cancelOrder(s1);
        assert(level.isEmpty());
        ClientOrder o2 = {10000, 200, 2, 0};
        uint8_t s2 = level.insertOrder(o2);
        assert(s2 == s1);
        assert(!level.isEmpty());
        assert(level.getHead() == s2);
        assert(level.getTail() == s2);
        printf("[PASS] Cancel then reinsert reuses slot\n");
    }

    // ─── TEST 9: Fill on empty level ───
    {
        PriceLevel level;
        assert(level.isEmpty());
        ClientOrder taker = {10000, 100, 9, 0};
        ClientOrder result = level.fillOrder(taker);
        assert(result.quantity == 100);
        assert(level.isEmpty());
        printf("[PASS] Fill on empty level\n");
    }

    // ─── TEST 10: Insert to max capacity, 256th returns 0xFF ───
    {
        PriceLevel level;
        for (int i = 0; i < 255; i++) {
            ClientOrder o = {10000, 10, (uint16_t)i, 0};
            uint8_t idx = level.insertOrder(o);
            assert(idx == (uint8_t)i);
        }
        assert(!level.isEmpty());
        ClientOrder o = {10000, 10, 255, 0};
        uint8_t over = level.insertOrder(o);
        assert(over == 0xFF);
        printf("[PASS] Max capacity, 256th insert returns 0xFF\n");
    }

    // ─── TEST 11: Cancel all, then refill to max ───
    {
        PriceLevel level;
        uint8_t slots[255];
        for (int i = 0; i < 255; i++) {
            ClientOrder o = {10000, 10, (uint16_t)i, 0};
            slots[i] = level.insertOrder(o);
            assert(slots[i] == (uint8_t)i);
        }
        for (int i = 0; i < 255; i++) {
            level.cancelOrder(slots[i]);
        }
        assert(level.isEmpty());
        for (int i = 0; i < 255; i++) {
            ClientOrder o = {10000, 10, (uint16_t)i, 0};
            uint8_t idx = level.insertOrder(o);
            assert(idx != 0xFF);
        }
        uint8_t over = level.insertOrder({10000, 10, 255, 0});
        assert(over == 0xFF);
        printf("[PASS] Cancel all, refill to max\n");
    }

    printf("\nAll tests passed!\n");
    return 0;
}