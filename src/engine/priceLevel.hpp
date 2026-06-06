#pragma once
#include <cstdint>
#include "order.hpp"

class alignas(64) PriceLevel
{
    //-----------VARIABLES--------------
    uint8_t freeIndexes[255];
    uint8_t stackTop;
    uint8_t head;
    uint8_t tail;
    //-----------------------------------
    public:
    EngineOrder orders[255]; 
    uint32_t totalQuantity; // total quantity at this price level, updated on insert, fill, and cancel
    //------------FUNCTIONS-------------
    PriceLevel();
    uint8_t insertOrder(const ClientOrder& order);
    FillResult fillOrder(const ClientOrder& order, uint32_t levelPrice, uint8_t side, GlobalOrderInfo* infos);
    void cancelOrder(uint8_t slotIndex); 
    uint8_t getHead() const { return head; }
    uint8_t getTail() const { return tail; }
    uint8_t isEmpty() const { return head == 0xFF; }
    //------------------------------------
};