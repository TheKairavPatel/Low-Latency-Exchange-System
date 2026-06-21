#pragma once
#include <cstdint>
#include "order.hpp"

class alignas(64) PriceLevel
{
    //-----------VARIABLES--------------
    uint8_t freeIndexes[255]; // stack of free indexes in the orders array, used for O(1) insertion and deletion
    uint8_t stackTop; // top of freeIndexes stack, 0 if all slots are free, 255 if all slots are occupied
    uint8_t head; // index of the next order to be filled chronologically
    uint8_t tail; // index of the most recently added order
    //-----------------------------------
    public:
    EngineOrder orders[255]; // array of orders at this price level, stored in a linked list manner using levelNext and levelPrev
    uint32_t totalQuantity; // total quantity at this price level, updated on insert, fill, and cancel
    //------------FUNCTIONS-------------
    PriceLevel();
    uint8_t insertOrder(const ClientOrder& order, GlobalOrderInfo* infos); // inserts an order into the price level, returns the index of the order in the orders array, or 255 if the price level is full
    FillResult fillOrder(const ClientOrder& order, uint32_t levelPrice, uint8_t side, GlobalOrderInfo* infos); // fills an incoming order against the orders in this price level, returns the remaining order and any events generated from the filling process
    void cancelOrder(uint8_t slotIndex); // cancels an order at the given index in the orders array
    uint8_t getHead() const { return head; } // index of the next order to be filled chronologically
    uint8_t getTail() const { return tail; } // index of the most recently added order
    uint8_t isEmpty() const { return head == 0xFF; } // whether the price level is empty, indicated by head being 255 (0xFF)
    //------------------------------------
};