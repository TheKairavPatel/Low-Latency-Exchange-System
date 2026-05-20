#include "engine.hpp"
#include <cstring>

Engine::Engine(uint32_t basePrice) : basePrice(basePrice)
{
    memset(buyBitmap, 0, sizeof(buyBitmap));
    memset(sellBitmap, 0, sizeof(sellBitmap));
    memset(globalOrderInfos, 0, sizeof(globalOrderInfos));
} 
