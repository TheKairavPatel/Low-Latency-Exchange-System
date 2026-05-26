#include <cstdint>
#pragma once

struct ClientOrder 
{
    uint32_t price;
    uint32_t quantity;
    uint16_t orderID;
    uint8_t type; // 0 for buy, 1 for sell, 2 for cancel, 3 for market buy, 4 for market sell
};

struct EngineOrder {
    uint32_t quantity;
    uint16_t orderID;
    uint8_t  levelNext;
    uint8_t  levelPrev;
};

struct GlobalOrderInfo
{
    uint16_t priceLevel;
    uint8_t posInArray;
    uint8_t side; // 0 for buy, 1 for sell
    bool live;
};

struct Event
{
    uint32_t price;
    uint32_t quantity;
    uint16_t orderID;
    uint8_t type; // 0 for cancel, 1 for fill
};

struct FillResult
{
    ClientOrder remaining;
    uint16_t filledIDs[255];
    uint8_t filledCount;
    Event events[32];
    uint8_t eventCount;
};