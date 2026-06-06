#include "engine.hpp"
#include <cstring>
#include <cstdio> // temp
#include <x86intrin.h> 
#include <algorithm> // temp

Engine::Engine(uint32_t basePrice, std::atomic<bool> &running) : basePrice(basePrice), running(running)
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

void Engine::markFilled(const FillResult& result)
{
    for (int i = 0; i < result.filledCount; i++)
        globalOrderInfos[result.filledIDs[i]].live = false;
}

void Engine::processOrder(const ClientOrder& order)
{
    //printf("printing");
    switch (order.type)
    {
        case 0:
        { // BUY
            uint16_t bestAsk = getBestAsk();
            uint16_t orderLevel = order.price - basePrice;

            if (bestAsk == LEVELS || orderLevel < bestAsk)
            {
                uint8_t slotIndex = buyLevels[orderLevel].insertOrder(order);
                if (slotIndex == 0xFF) [[unlikely]]
                {
                    Event rejectEvent = {order.price, order.quantity, order.orderID, 0, uint8_t((order.type == 0 || order.type == 3) ? 0 : 1), true};
                    outboundQueue.push(rejectEvent);
                }
                else
                {
                    globalOrderInfos[order.orderID] = {orderLevel, slotIndex, 0, true};
                    buyBitmap[orderLevel/64] |= (1ULL << (orderLevel % 64));
                }
            }
            else
            {
                FillResult result = sellLevels[bestAsk].fillOrder(order, bestAsk + basePrice, 1);
                
                for (int i = 0; i < result.eventCount; i++)
                {
                    outboundQueue.push(result.events[i]);
                }
                markFilled(result);
                if (sellLevels[bestAsk].isEmpty())
                    sellBitmap[bestAsk/64] &= ~(1ULL << (bestAsk % 64));

                while (result.remaining.quantity > 0 && bestAsk <= orderLevel)
                {
                    bestAsk = getBestAsk();
                    if (bestAsk == LEVELS || bestAsk > orderLevel)
                    {
                        uint8_t slotIndex = buyLevels[orderLevel].insertOrder(result.remaining);
                        if (slotIndex == 0xFF) [[unlikely]]
                        {
                            Event rejectEvent = {result.remaining.price, result.remaining.quantity, result.remaining.orderID, 0, uint8_t((order.type == 0 || order.type == 3) ? 0 : 1), true};
                            outboundQueue.push(rejectEvent);
                        }
                        else
                        {
                            globalOrderInfos[order.orderID] = {orderLevel, slotIndex, 0, true};
                            buyBitmap[orderLevel/64] |= (1ULL << (orderLevel % 64));
                        }
                        break;
                    }
                    else
                    {
                        result = sellLevels[bestAsk].fillOrder(result.remaining, basePrice + bestAsk, 1);

                        for (int i = 0; i < result.eventCount; i++)
                        {
                                outboundQueue.push(result.events[i]);
                        }

                        markFilled(result);
                        if (sellLevels[bestAsk].isEmpty())
                            sellBitmap[bestAsk/64] &= ~(1ULL << (bestAsk % 64));
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
                uint8_t slotIndex = sellLevels[orderLevel].insertOrder(order);
                if (slotIndex == 0xFF) [[unlikely]]
                {
                    Event rejectEvent = {order.price, order.quantity, order.orderID, 0, uint8_t((order.type == 0 || order.type == 3) ? 0 : 1), true};
                    outboundQueue.push(rejectEvent);
                }
                else
                {
                    globalOrderInfos[order.orderID] = {orderLevel, slotIndex, 1, true};
                    sellBitmap[orderLevel/64] |= (1ULL << (orderLevel % 64));
                }
            }
            else
            {
                FillResult result = buyLevels[bestBid].fillOrder(order, bestBid + basePrice, 0);
                        
                for (int i = 0; i < result.eventCount; i++)
                {
                    outboundQueue.push(result.events[i]);
                }

                markFilled(result);
                if (buyLevels[bestBid].isEmpty())
                    buyBitmap[bestBid/64] &= ~(1ULL << (bestBid % 64));

                while (result.remaining.quantity > 0 && bestBid >= orderLevel)
                {
                    bestBid = getBestBid();
                    if (bestBid == LEVELS || bestBid < orderLevel)
                    {
                        uint8_t slotIndex = sellLevels[orderLevel].insertOrder(result.remaining);
                        if (slotIndex == 0xFF) [[unlikely]]
                        {
                            Event rejectEvent = {result.remaining.price, result.remaining.quantity, result.remaining.orderID, 0, uint8_t((order.type == 0 || order.type == 3) ? 0 : 1), true};
                            outboundQueue.push(rejectEvent);
                        }
                        else
                        {
                            globalOrderInfos[order.orderID] = {orderLevel, slotIndex, 1, true};
                            sellBitmap[orderLevel/64] |= (1ULL << (orderLevel % 64));
                        }
                        break;
                    }
                    else
                    {
                        result = buyLevels[bestBid].fillOrder(result.remaining, basePrice + bestBid, 0);

                        for (int i = 0; i < result.eventCount; i++)
                        {
                            outboundQueue.push(result.events[i]);
                        }

                        markFilled(result);
                        if (buyLevels[bestBid].isEmpty())
                            buyBitmap[bestBid/64] &= ~(1ULL << (bestBid % 64));
                    }
                }
            }
            break;
        }
        case 2: // CANCEL
        {
            if (!globalOrderInfos[order.orderID].live) break; // already filled, ignore
            uint16_t priceLevel = globalOrderInfos[order.orderID].priceLevel;
            uint8_t slotIndex = globalOrderInfos[order.orderID].posInArray;
            uint16_t quantity = globalOrderInfos[order.orderID].side == 0 ? buyLevels[priceLevel].orders[slotIndex].quantity : sellLevels[priceLevel].orders[slotIndex].quantity;

            Event cancelEvent = {globalOrderInfos[order.orderID].priceLevel+basePrice, quantity, order.orderID, 0, globalOrderInfos[order.orderID].side, true};
            outboundQueue.push(cancelEvent);

            globalOrderInfos[order.orderID].live = false;
            if (globalOrderInfos[order.orderID].side == 0)
            {
                buyLevels[priceLevel].cancelOrder(slotIndex);
                if (buyLevels[priceLevel].isEmpty())
                    buyBitmap[priceLevel/64] &= ~(1ULL << (priceLevel % 64));
            }
            else
            {
                sellLevels[priceLevel].cancelOrder(slotIndex);
                if (sellLevels[priceLevel].isEmpty())
                    sellBitmap[priceLevel/64] &= ~(1ULL << (priceLevel % 64));
            }
            // temp print
            break;
        }
        case 3: // MARKET BUY
        { 
            uint16_t bestAsk = getBestAsk();
            FillResult result;
            result.remaining = order; // initialize remaining

            while (result.remaining.quantity > 0 && bestAsk != LEVELS)
            {
                result = sellLevels[bestAsk].fillOrder(result.remaining, basePrice + bestAsk, 1);

                for (int i = 0; i < result.eventCount; i++)
                {
                    outboundQueue.push(result.events[i]);
                }

                markFilled(result);
                if (sellLevels[bestAsk].isEmpty())
                {
                    sellBitmap[bestAsk/64] &= ~(1ULL << (bestAsk % 64));
                    bestAsk = getBestAsk();
                }
            }
            Event marketDoneEvent = {order.price, result.remaining.quantity, order.orderID, 1, 0, true};
            outboundQueue.push(marketDoneEvent);
            break;
        }
        case 4: // MARKET SELL
        {
            uint16_t bestBid = getBestBid();
            FillResult result;
            result.remaining = order; // initialize remaining

            while (result.remaining.quantity > 0 && bestBid != LEVELS)
            {
                result = buyLevels[bestBid].fillOrder(result.remaining, basePrice + bestBid, 0);
                for (int i = 0; i < result.eventCount; i++)
                {
                    outboundQueue.push(result.events[i]);
                }

                markFilled(result);
                if (buyLevels[bestBid].isEmpty())
                {
                    buyBitmap[bestBid/64] &= ~(1ULL << (bestBid % 64));
                    bestBid = getBestBid();
                }
            }
            // remainder is implicitly cancelled
            Event marketDoneEvent = {order.price, result.remaining.quantity, order.orderID, 1, 1, true};
            outboundQueue.push(marketDoneEvent);
            break;
        }
    }
}

void Engine::run()
{
    static uint64_t samples[500000];
    int sampleCount = 0;

    ClientOrder order;
    while (running.load(std::memory_order_relaxed))
    {
        while (inboundQueue.pop(order))
        {
            uint64_t s = __rdtsc();
            processOrder(order);
            uint64_t e = __rdtsc();
            if (sampleCount < 500000)
                samples[sampleCount++] = e - s;
        }
    }

    if (sampleCount == 0) return;

    std::sort(samples, samples + sampleCount);

    auto pct = [&](double p) -> uint64_t {
        return samples[(size_t)(p * (sampleCount - 1))];
    };

    uint64_t sum = 0;
    for (int i = 0; i < sampleCount; i++) sum += samples[i];

    printf("\n=== ENGINE BENCHMARK (%d samples) ===\n", sampleCount);
    printf("  mean : %.2f cycles\n", (double)sum / sampleCount);
    printf("  p50  : %lu cycles\n", pct(0.50));
    printf("  p90  : %lu cycles\n", pct(0.90));
    printf("  p99  : %lu cycles\n", pct(0.99));
    printf("  p99.9: %lu cycles\n", pct(0.999));
    printf("  max  : %lu cycles\n", samples[sampleCount - 1]);

    uint64_t cutoff = pct(0.999);
    int filtered = 0;
    uint64_t filteredSum = 0;
    for (int i = 0; i < sampleCount; i++) {
        if (samples[i] <= cutoff) {
            filteredSum += samples[i];
            filtered++;
        }
    }

    printf("\n=== FILTERED (p99.9 cutoff=%lu cycles) ===\n", cutoff);
    printf("  mean : %.2f cycles\n", (double)filteredSum / filtered);
    printf("  p50  : %lu cycles\n", pct(0.50));
    printf("  p90  : %lu cycles\n", pct(0.90));
    printf("  p99  : %lu cycles\n", pct(0.99));
    printf("  p99.9: %lu cycles\n", pct(0.999));
}