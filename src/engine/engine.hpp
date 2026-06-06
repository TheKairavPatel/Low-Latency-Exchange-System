#pragma once
#include <atomic>
#include "priceLevel.hpp"
#include "../gateway/Queues.hpp"

static constexpr uint32_t LEVELS = 2048;

class Engine
{
    GlobalOrderInfo globalOrderInfos[65536];
    std::atomic<bool>& running;
    uint64_t buyBitmap[32];
    uint64_t sellBitmap[32];

    public:
    uint32_t basePrice;
    PriceLevel buyLevels[LEVELS];
    PriceLevel sellLevels[LEVELS];
    OutboundQueue outboundQueue;
    InboundQueue inboundQueue;

    Engine(uint32_t basePrice, std::atomic<bool>& running);
    void processOrder(const ClientOrder& order);
    uint16_t getBestBid();
    uint16_t getBestAsk();
    void run();

    private:
    void markFilled(const FillResult& result);
};