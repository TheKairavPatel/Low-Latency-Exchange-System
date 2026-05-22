#include "engine.hpp"
#include <cstring>

Engine::Engine(uint32_t basePrice) : basePrice(basePrice)
{
    memset(buyBitmap, 0, sizeof(buyBitmap));
    memset(sellBitmap, 0, sizeof(sellBitmap));
    memset(globalOrderInfos, 0, sizeof(globalOrderInfos));
}

uint16_t Engine::getBestAsk()
{
    for (int i = 0; i < 32; i++)
    {
        if (sellBitmap[i] != 0)
        {
            return i*64 + __builtin_ctzll(sellBitmap[i]);
        }
    }
    return LEVELS; // No asks available
}

uint16_t Engine::getBestBid()
{
    for (int i = 31; i >= 0; i--)
    {
        if (buyBitmap[i] != 0)
        {
            return i*64 + (63 - __builtin_clzll(buyBitmap[i]));
        }
    }
    return LEVELS; // No bids available
}