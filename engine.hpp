#pragma once
#include "priceLevel.hpp"
#include "Queues.hpp"

static constexpr uint32_t LEVELS = 2048;

class Engine
{
    uint32_t basePrice; // this is the price at bottom of the book
    PriceLevel buyLevels[LEVELS]; // 1000 ticks above and 1000 ticks below the base price
    PriceLevel sellLevels[LEVELS];
    GlobalOrderInfo globalOrderInfos[65536];
    uint64_t buyBitmap[32];
    uint64_t sellBitmap[32];
    std::atomic<bool>& running;

    public:
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