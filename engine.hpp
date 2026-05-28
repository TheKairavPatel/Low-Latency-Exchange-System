#include "priceLevel.hpp"
#include "Outbound.hpp"

static constexpr uint32_t LEVELS = 2048;

class Engine
{
    uint32_t basePrice; // this is the price at bottom of the book
    PriceLevel buyLevels[LEVELS]; // 1000 ticks above and 1000 ticks below the base price
    PriceLevel sellLevels[LEVELS];
    GlobalOrderInfo globalOrderInfos[65536];
    uint64_t buyBitmap[32];
    uint64_t sellBitmap[32];

    public:
    Engine(uint32_t basePrice);
    void processOrder(const ClientOrder& order);
    uint16_t getBestBid();
    uint16_t getBestAsk();
    OutboundQueue outboundQueue;
    private:
    void markFilled(const FillResult& result);
    
};