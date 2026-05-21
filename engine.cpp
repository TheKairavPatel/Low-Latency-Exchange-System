#include "engine.hpp"
#include <cstring>

Engine::Engine(uint32_t basePrice) : basePrice(basePrice)
{
    memset(buyBitmap, 0, sizeof(buyBitmap));
    memset(sellBitmap, 0, sizeof(sellBitmap));
    memset(globalOrderInfos, 0, sizeof(globalOrderInfos));
}

void Engine::processAdd(const ClientOrder& order)
{
    uint16_t priceLevel = order.price - basePrice + 1000;
    uint8_t slotIndex;
    if (order.type == 0) // buy
    {
        slotIndex = buyLevels[priceLevel].insertOrder(order);
        buyBitmap[priceLevel / 64] |= (1ULL << (priceLevel % 64));
    }
    else if (order.type == 1) // sell
    {
        slotIndex = sellLevels[priceLevel].insertOrder(order);
        sellBitmap[priceLevel / 64] |= (1ULL << (priceLevel % 64));
    }
    globalOrderInfos[order.orderID] = {priceLevel, slotIndex, order.type};
}

void Engine::processCancel(const ClientOrder& order)
{
    GlobalOrderInfo info = globalOrderInfos[order.orderID];
    if (info.side == 0)
    {
        buyLevels[info.priceLevel].cancelOrder(info.posInArray);
        if (buyLevels[info.priceLevel].isEmpty())
            buyBitmap[info.priceLevel / 64] &= ~(1ULL << (info.priceLevel % 64));
    }
    else
    {
        sellLevels[info.priceLevel].cancelOrder(info.posInArray);
        if (sellLevels[info.priceLevel].isEmpty())
            sellBitmap[info.priceLevel / 64] &= ~(1ULL << (info.priceLevel % 64));
    }
}