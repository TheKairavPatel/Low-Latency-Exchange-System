#include "engine.hpp"
#include <cstring>
#include <cstdio>
#include <x86intrin.h>
#include <algorithm>

Engine::Engine(uint32_t basePrice, std::atomic<bool>& running) : basePrice(basePrice), running(running)
{
    memset(buyBitmap, 0, sizeof(buyBitmap)); // set all bits to 0 of buy bitmap
    memset(sellBitmap, 0, sizeof(sellBitmap)); // set all bits to 0 of sell bitmap
    memset(globalOrderInfos, 0, sizeof(globalOrderInfos)); // set all original bits of lookup table to 0
    totalOrders = 0; // original number of orders processed is 0
}

uint16_t Engine::getBestAsk() // returns indice of lowest price level with a ask order present
{
    for (int i = 0; i < 32; i++) // loop through all uint64s until a bit with a 1 is found
    {
        if (sellBitmap[i] != 0)
            return i*64 + __builtin_ctzll(sellBitmap[i]); // use native assembly instruction to find number of trailing zeros before first 1 bit adding number of bits we have already gone through
    }
    return LEVELS; // if no 1 bits found in bitmap, return LEVELS indicating no orders present in ask book
}

uint16_t Engine::getBestBid() // returns indice of highest price level with a bid order present
{
    for (int i = 31; i >= 0; i--) // loop through all uint64s until a bit with a 1 is found
    {
        if (buyBitmap[i] != 0) // if not all 0s in the current section of the bitmap (uint64)
            return i*64 + (63 - __builtin_clzll(buyBitmap[i])); // use native assembly instruction to find number of leading zeros in current uint64 and add it to number of bits we already iterated through
    }
    return LEVELS; // if no 1 bits found in bitmap, return LEVELS indicating no orders present in bid book
}

void Engine::processOrder(const ClientOrder& order)
{
    if (order.type != 2) // if not a cancel, send ACK immediately
    {
        uint8_t side = (order.type == 1 || order.type == 4) ? 1 : 0; // side = 1 for sell/market sell acks, 0 for buy/market buy acks
        outboundQueue.push({order.price, order.quantity, order.orderID, 2, side, false, {}}); // push ack of original incoming order info
    }
    switch (order.type)
    {
        case 0:
        { // BUY
            uint16_t bestAsk = getBestAsk(); // get best available price index of sell book
            uint16_t orderLevel = order.price - basePrice; // get the index corresponding to new order 

            if (bestAsk == LEVELS || orderLevel < bestAsk) // if no sell orders are none below asking price
            {
                uint8_t slotIndex = buyLevels[orderLevel].insertOrder(order); // store into correct pricelevel in buy book
                if (slotIndex == 0xFF) [[unlikely]] // 0xFF signals the price level's order array is full
                {
                    Event rejectEvent = {order.price, order.quantity, order.orderID, 0, 0, true}; // build reject event for this buy order
                    outboundQueue.push(rejectEvent); // reject order if pricelevel is full
                }
                else
                {
                    globalOrderInfos[order.orderID] = {orderLevel, slotIndex, 0, true}; // store order info in lookup table 
                    buyBitmap[orderLevel/64] |= (1ULL << (orderLevel % 64)); // turn bit corresponding to that buy level as active
                }
            }
            else
            {
                FillResult result = sellLevels[bestAsk].fillOrder(order, bestAsk + basePrice, 1, globalOrderInfos); // fill the order against best sell
                for (int i = 0; i < result.eventCount; i++)
                    outboundQueue.push(result.events[i]); // push any events that occurred
                if (sellLevels[bestAsk].isEmpty()) 
                    sellBitmap[bestAsk/64] &= ~(1ULL << (bestAsk % 64)); // turn bit corresponding to current sell level is inactive if fully empty

                while (result.remaining.quantity > 0 && bestAsk <= orderLevel) // continue filling until best order 
                {
                    bestAsk = getBestAsk(); // refresh best ask, sell side may have changed
                    if (bestAsk == LEVELS || bestAsk > orderLevel) // no more asks within our limit price, rest the remainder in the buy book
                    {
                        uint8_t slotIndex = buyLevels[orderLevel].insertOrder(result.remaining); // insert leftover quantity at our original order level
                        if (slotIndex == 0xFF) [[unlikely]] // reject leftover quantity if price level is full
                        {
                            Event rejectEvent = {result.remaining.price, result.remaining.quantity, result.remaining.orderID, 0, 0, true}; // build reject event for the unfilled remainder
                            outboundQueue.push(rejectEvent); // send reject
                        }
                        else
                        {
                            globalOrderInfos[order.orderID] = {orderLevel, slotIndex, 0, true}; // store order info in lookup table
                            buyBitmap[orderLevel/64] |= (1ULL << (orderLevel % 64)); // activate bit for this buy level
                        }
                        break; // remainder now resting, stop matching loop
                    }
                    else
                    {
                        result = sellLevels[bestAsk].fillOrder(result.remaining, basePrice + bestAsk, 1, globalOrderInfos); // match remainder against next best ask
                        for (int i = 0; i < result.eventCount; i++)
                            outboundQueue.push(result.events[i]); // push any fill/trade events from this match
                        if (sellLevels[bestAsk].isEmpty())
                            sellBitmap[bestAsk/64] &= ~(1ULL << (bestAsk % 64)); // clear bit since sell level is now empty
                    }
                }
            }
            break;
        }
        case 1:
        { // SELL
            uint16_t bestBid = getBestBid(); // get best available price index of buy book
            uint16_t orderLevel = order.price - basePrice; // get the index corresponding to new order

            if (bestBid == LEVELS || orderLevel > bestBid) // if no buy orders are at or above asking price
            {
                uint8_t slotIndex = sellLevels[orderLevel].insertOrder(order); // store into correct pricelevel in sell book
                if (slotIndex == 0xFF) [[unlikely]] // 0xFF signals the price level's order array is full
                {
                    Event rejectEvent = {order.price, order.quantity, order.orderID, 0, 1, true}; // build reject event for this sell order
                    outboundQueue.push(rejectEvent); // reject order if pricelevel is full
                }
                else
                {
                    globalOrderInfos[order.orderID] = {orderLevel, slotIndex, 1, true}; // store order info in lookup table
                    sellBitmap[orderLevel/64] |= (1ULL << (orderLevel % 64)); // turn bit corresponding to that sell level as active
                }
            }
            else
            {
                FillResult result = buyLevels[bestBid].fillOrder(order, bestBid + basePrice, 0, globalOrderInfos); // fill the order against best buy
                for (int i = 0; i < result.eventCount; i++)
                    outboundQueue.push(result.events[i]); // push any events that occurred
                if (buyLevels[bestBid].isEmpty())
                    buyBitmap[bestBid/64] &= ~(1ULL << (bestBid % 64)); // turn bit corresponding to current buy level inactive if fully empty

                while (result.remaining.quantity > 0 && bestBid >= orderLevel) // continue filling until best order
                {
                    bestBid = getBestBid(); // refresh best bid, buy side may have changed
                    if (bestBid == LEVELS || bestBid < orderLevel) // no more bids within our limit price, rest the remainder in the sell book
                    {
                        uint8_t slotIndex = sellLevels[orderLevel].insertOrder(result.remaining); // insert leftover quantity at our original order level
                        if (slotIndex == 0xFF) [[unlikely]] // reject leftover quantity if price level is full
                        {
                            Event rejectEvent = {result.remaining.price, result.remaining.quantity, result.remaining.orderID, 0, 1, true}; // build reject event for the unfilled remainder
                            outboundQueue.push(rejectEvent); // send reject
                        }
                        else
                        {
                            globalOrderInfos[order.orderID] = {orderLevel, slotIndex, 1, true}; // store order info in lookup table
                            sellBitmap[orderLevel/64] |= (1ULL << (orderLevel % 64)); // activate bit for this sell level
                        }
                        break; // remainder now resting, stop matching loop
                    }
                    else
                    {
                        result = buyLevels[bestBid].fillOrder(result.remaining, basePrice + bestBid, 0, globalOrderInfos); // match remainder against next best bid
                        for (int i = 0; i < result.eventCount; i++)
                            outboundQueue.push(result.events[i]); // push any fill/trade events from this match
                        if (buyLevels[bestBid].isEmpty())
                            buyBitmap[bestBid/64] &= ~(1ULL << (bestBid % 64)); // clear bit since buy level is now empty
                    }
                }
            }
            break;
        }
        case 2: // CANCEL
        {
            auto& info = globalOrderInfos[order.orderID]; // look up order's location via the lookup table
            if (!info.live) break; // ignore cancel if order is already gone
            uint16_t priceLevel = info.priceLevel; // price level the order is resting at
            uint8_t slotIndex = info.posInArray; // slot within that price level's order array
            uint8_t side = info.side; // 0 = buy book, 1 = sell book
            uint16_t quantity = side == 0
                ? buyLevels[priceLevel].orders[slotIndex].quantity
                : sellLevels[priceLevel].orders[slotIndex].quantity; // grab remaining quantity before we cancel it

            Event cancelEvent = {priceLevel + basePrice, quantity, order.orderID, 0, side, true}; // build cancel confirmation event
            outboundQueue.push(cancelEvent); // notify client the order was cancelled
            info.live = false; // mark order as no longer live in lookup table

            if (side == 0)
            {
                buyLevels[priceLevel].cancelOrder(slotIndex); // remove order from buy book
                if (buyLevels[priceLevel].isEmpty())
                    buyBitmap[priceLevel/64] &= ~(1ULL << (priceLevel % 64)); // clear bit if buy level now empty
            }
            else
            {
                sellLevels[priceLevel].cancelOrder(slotIndex); // remove order from sell book
                if (sellLevels[priceLevel].isEmpty())
                    sellBitmap[priceLevel/64] &= ~(1ULL << (priceLevel % 64)); // clear bit if sell level now empty
            }
            break;
        }
        case 3: // MARKET BUY
        {
            uint16_t bestAsk = getBestAsk(); // get best available price index of sell book
            FillResult result;
            result.remaining = order; // start with the full market order as remaining

            while (result.remaining.quantity > 0 && bestAsk != LEVELS) // keep sweeping the book until filled or out of asks
            {
                result = sellLevels[bestAsk].fillOrder(result.remaining, basePrice + bestAsk, 1, globalOrderInfos); // fill against current best ask
                for (int i = 0; i < result.eventCount; i++)
                    outboundQueue.push(result.events[i]); // push any fill/trade events from this match
                if (sellLevels[bestAsk].isEmpty())
                {
                    sellBitmap[bestAsk/64] &= ~(1ULL << (bestAsk % 64)); // clear bit since sell level is now empty
                    bestAsk = getBestAsk(); // move on to next best ask
                }
            }
            Event marketDoneEvent = {order.price, result.remaining.quantity, order.orderID, 1, 0, true}; // report leftover unfilled quantity, if any
            outboundQueue.push(marketDoneEvent); // notify client the market order is done
            break;
        }
        case 4: // MARKET SELL
        {
            uint16_t bestBid = getBestBid(); // get best available price index of buy book
            FillResult result;
            result.remaining = order; // start with the full market order as remaining

            while (result.remaining.quantity > 0 && bestBid != LEVELS) // keep sweeping the book until filled or out of bids
            {
                result = buyLevels[bestBid].fillOrder(result.remaining, basePrice + bestBid, 0, globalOrderInfos); // fill against current best bid
                for (int i = 0; i < result.eventCount; i++)
                    outboundQueue.push(result.events[i]); // push any fill/trade events from this match
                if (buyLevels[bestBid].isEmpty())
                {
                    buyBitmap[bestBid/64] &= ~(1ULL << (bestBid % 64)); // clear bit since buy level is now empty
                    bestBid = getBestBid(); // move on to next best bid
                }
            }
            Event marketDoneEvent = {order.price, result.remaining.quantity, order.orderID, 1, 1, true}; // report leftover unfilled quantity, if any
            outboundQueue.push(marketDoneEvent); // notify client the market order is done
            break;
        }
    }
    totalOrders++; // increment total processed order count
}

void Engine::run()
{
    static uint64_t samples[100000000]; // big buffer to hold per-order latency samples in cycles
    int sampleCount = 0; // number of samples collected so far

    ClientOrder order;
    while (running.load(std::memory_order_relaxed)) // main benchmark loop, runs until shutdown signal
    {
        while (inboundQueue.pop(order)) // drain whatever orders are waiting
        {
            uint64_t s = __rdtsc(); // timestamp before processing
            processOrder(order);
            uint64_t e = __rdtsc(); // timestamp after processing
            if (sampleCount < 100000000)
                samples[sampleCount++] = e - s; // record latency in cycles for this order
        }
    }

    if (sampleCount == 0) return; // nothing to report

    std::sort(samples, samples + sampleCount); // sort so we can pull percentiles by index

    auto pct = [&](double p) -> uint64_t {
        return samples[(size_t)(p * (sampleCount - 1))]; // index into sorted samples for the given percentile
    };

    uint64_t sum = 0;
    for (int i = 0; i < sampleCount; i++) sum += samples[i]; // accumulate total cycles for mean

    printf("\n=== ENGINE BENCHMARK (%d samples) ===\n", sampleCount);
    printf("  mean : %.2f cycles\n", (double)sum / sampleCount);
    printf("  p50  : %lu cycles\n", pct(0.50));
    printf("  p90  : %lu cycles\n", pct(0.90));
    printf("  p99  : %lu cycles\n", pct(0.99));
    printf("  p99.9: %lu cycles\n", pct(0.999));
    printf("  max  : %lu cycles\n", samples[sampleCount - 1]); // worst observed latency
}

void Engine::runDemo()
{
    ClientOrder order;
    while (running.load(std::memory_order_relaxed)) // main demo loop, runs until shutdown signal
    {
        while (inboundQueue.pop(order)) // process every order currently queued
            processOrder(order);
    }
}