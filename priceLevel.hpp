#include <cstdint>
#include "order.hpp"
#pragma once

class alignas(64) PriceLevel
{
    uint8_t freeIndexes[255];
    uint8_t stackTop;
    uint8_t head;
    uint8_t tail;
    EngineOrder orders[255]; 
    public:
    PriceLevel();
    uint8_t insertOrder(const ClientOrder& order);
    uint32_t fillOrder(const ClientOrder& order); // returns the quantity left
    void cancelOrder(uint8_t slotIndex); 
    uint8_t getHead() const { return head; }
    uint8_t getTail() const { return tail; }
    uint8_t isEmpty() const { return head == 0xFF; }
};