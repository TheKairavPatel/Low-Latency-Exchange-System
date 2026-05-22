#include "engine.hpp"
#include <cstring>

Engine::Engine(uint32_t basePrice) : basePrice(basePrice)
{
    memset(buyBitmap, 0, sizeof(buyBitmap));
    memset(sellBitmap, 0, sizeof(sellBitmap));
    memset(globalOrderInfos, 0, sizeof(globalOrderInfos));
}

uint16_t Engine::getBestAsk() // returns lowest price level with available sell orders
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

uint16_t Engine::getBestBid() // returns highest price level with available buy orders
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

void Engine::processOrder(const ClientOrder& order)
{
    // Implementation of order processing logic goes here
    // This will involve matching orders, updating price levels, and managing the order book
    switch (order.type)
    {
        case 0:
            { // BUY
            uint16_t bestAsk = getBestAsk();
            if (bestAsk == LEVELS || ((order.price - basePrice) < bestAsk)) 
            {
                // Add to buy book
                uint16_t priceLevel = order.price - basePrice;
                uint8_t slotIndex = buyLevels[priceLevel].insertOrder(order);
                if (slotIndex != 0xFF)
                {
                    globalOrderInfos[order.orderID].priceLevel = priceLevel;
                    globalOrderInfos[order.orderID].posInArray = slotIndex;
                    globalOrderInfos[order.orderID].side = 0; // Buy side
                    buyBitmap[priceLevel/64] |= (1ULL << (priceLevel % 64)); // Mark this price level as having orders
                }
            }
            else
            {
                ClientOrder temp = sellLevels[bestAsk].fillOrder(order); // Match with best ask
                if (sellLevels[bestAsk].isEmpty())
                {
                    sellBitmap[bestAsk/64] &= ~(1ULL << (bestAsk % 64)); // Unmark this price level if it's empty
                }
                while (temp.quantity > 0 && bestAsk <= order.price - basePrice)
                {
                    bestAsk = getBestAsk();
                    if (bestAsk == LEVELS || bestAsk > order.price - basePrice)
                    {
                        globalOrderInfos[order.orderID].priceLevel = order.price - basePrice;
                        globalOrderInfos[order.orderID].posInArray = buyLevels[order.price - basePrice].insertOrder(temp);
                        globalOrderInfos[order.orderID].side = 0;
                        buyBitmap[(order.price - basePrice)/64] |= (1ULL << ((order.price - basePrice) % 64)); // Mark this price level as having orders
                        break;
                    }
                    else
                    {
                        temp = sellLevels[bestAsk].fillOrder(temp);
                        if (sellLevels[bestAsk].isEmpty())
                        {
                            sellBitmap[bestAsk/64] &= ~(1ULL << (bestAsk % 64)); // Unmark this price level if it's empty
                        }
                    }
                }
            }
            break;
        }
        case 1:
        { // SELL
            uint16_t bestBid = getBestBid();
            uint16_t orderLevel = order.price - basePrice;

            if (bestBid == LEVELS || orderLevel > bestBid)
            {
                // no match, insert into sell book
                uint8_t slotIndex = sellLevels[orderLevel].insertOrder(order);
                if (slotIndex != 0xFF)
                {
                    globalOrderInfos[order.orderID].priceLevel = orderLevel;
                    globalOrderInfos[order.orderID].posInArray = slotIndex;
                    globalOrderInfos[order.orderID].side = 1;
                    sellBitmap[orderLevel/64] |= (1ULL << (orderLevel % 64));
                }
            }
            else
            {
                ClientOrder temp = buyLevels[bestBid].fillOrder(order);
                if (buyLevels[bestBid].isEmpty())
                    buyBitmap[bestBid/64] &= ~(1ULL << (bestBid % 64));

                while (temp.quantity > 0 && bestBid >= orderLevel)
                {
                    bestBid = getBestBid();
                    if (bestBid == LEVELS || bestBid < orderLevel)
                    {
                        globalOrderInfos[order.orderID].priceLevel = orderLevel;
                        globalOrderInfos[order.orderID].posInArray = sellLevels[orderLevel].insertOrder(temp);
                        globalOrderInfos[order.orderID].side = 1;
                        sellBitmap[orderLevel/64] |= (1ULL << (orderLevel % 64));
                        break;
                    }
                    else
                    {
                        temp = buyLevels[bestBid].fillOrder(temp);
                        if (buyLevels[bestBid].isEmpty())
                            buyBitmap[bestBid/64] &= ~(1ULL << (bestBid % 64));
                    }
                }
            }
            break;
        }
        case 2: // CANCEL
        {
            uint16_t priceLevel = globalOrderInfos[order.orderID].priceLevel;
            uint8_t slotIndex = globalOrderInfos[order.orderID].posInArray;
            if (globalOrderInfos[order.orderID].side == 0) // Buy side
            {
                buyLevels[priceLevel].cancelOrder(slotIndex);
                if (buyLevels[priceLevel].isEmpty())
                {
                    buyBitmap[priceLevel/64] &= ~(1ULL << (priceLevel % 64)); // Unmark this price level if it's empty
                }
            }
            else // Sell side
            {
                sellLevels[priceLevel].cancelOrder(slotIndex);
                if (sellLevels[priceLevel].isEmpty())
                {
                    sellBitmap[priceLevel/64] &= ~(1ULL << (priceLevel % 64)); // Unmark this price level if it's empty
                }
            }
            break;
        }
    }
}