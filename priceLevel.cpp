#include "priceLevel.hpp"
using namespace std;

PriceLevel::PriceLevel()
{
    stackTop = 0;
    head = 0xFF;
    tail = 0xFF;
    for (int i = 0; i < 255; i++) 
    {
        freeIndexes[i] = (uint8_t)i;
    }
}

uint8_t PriceLevel::insertOrder(const ClientOrder& order)
{
    if (stackTop == 255) 
    {
        return 0xFF; // No space for new orders
    }
    uint8_t newIndex = freeIndexes[stackTop++];
    orders[newIndex].quantity = order.quantity;
    orders[newIndex].orderID = order.orderID;
    orders[newIndex].levelNext = 0xFF;
    orders[newIndex].levelPrev = 0xFF;

    if (head == 0xFF) 
    {
        head = tail = newIndex; // First order in the level
    } 
    else 
    {
        orders[tail].levelNext = newIndex; // Link the new order at the end
        orders[newIndex].levelPrev = tail;
        tail = newIndex; // Update tail to the new order
    }
    return newIndex;
}

void PriceLevel::cancelOrder(uint8_t slotIndex)
{
    uint8_t prevIndex = orders[slotIndex].levelPrev;
    uint8_t nextIndex = orders[slotIndex].levelNext;
    stackTop--;
    freeIndexes[stackTop] = slotIndex; 
    if (prevIndex == 0xFF)
    {
        head = nextIndex;
    }
    else
    {
        orders[prevIndex].levelNext = nextIndex;
    }
    if (nextIndex == 0xFF)
    {
        tail = prevIndex;
    }
    else
    {
        orders[nextIndex].levelPrev = prevIndex;
    }
}

uint32_t PriceLevel::fillOrder(const ClientOrder& order)
{
    uint8_t current = head;
    uint32_t remaining = order.quantity;

    while (current != 0xFF && remaining > 0)
    {
        EngineOrder &node = orders[current];
        uint8_t next = node.levelNext;

        // compute fill once (branch-light)
        uint32_t fill = (remaining < node.quantity)
                        ? remaining
                        : node.quantity;

        node.quantity -= fill;
        remaining -= fill;

        // if fully filled → remove inline (no function call)
        if (node.quantity == 0)
        {
            uint8_t prev = node.levelPrev;

            // unlink from list
            if (prev == 0xFF)
                head = next;
            else
                orders[prev].levelNext = next;

            if (next == 0xFF)
                tail = prev;
            else
                orders[next].levelPrev = prev;

            // push to free stack (O(1))
            freeIndexes[stackTop++] = current;
        }

        current = next;
    }

    return remaining;
}