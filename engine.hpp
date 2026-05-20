#include "priceLevel.hpp"

class Engine
{
    uint32_t basePrice; // Engine will covert baseprice +- 1000 ticks
    PriceLevel buyLevels[2001]; // 1000 ticks above and 1000 ticks below the base price
    PriceLevel sellLevels[2001];
    GlobalOrderInfo globalOrderInfos[65536];
    uint64_t buyBitmap[32];
    uint64_t sellBitmap[32];
    public:
    
    Engine(uint32_t basePrice);
    void processOrder(const ClientOrder& order);
    
    private:
    void processAdd(const ClientOrder& order);
    void processCancel(const ClientOrder& order);
    void processMatch(const ClientOrder& order);
    uint16_t getBestAsk();
    uint16_t getBestBid();
};