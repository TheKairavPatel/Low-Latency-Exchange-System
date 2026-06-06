#include "priceLevel.hpp"

PriceLevel::PriceLevel()
{
    stackTop = 0;
    head = 0xFF;
    tail = 0xFF;
    for (int i = 0; i < 255; i++) 
    {
        freeIndexes[i] = (uint8_t)i;
    }
    totalQuantity = 0;
}

uint8_t PriceLevel::insertOrder(const ClientOrder& order)
{
    if (stackTop == 255) [[unlikely]]
    {
        return 0xFF;
    }
    uint8_t newIndex = freeIndexes[stackTop];
    stackTop++;
    orders[newIndex].quantity = order.quantity;
    orders[newIndex].orderID = order.orderID;
    orders[newIndex].levelNext = 0xFF;
    orders[newIndex].levelPrev = 0xFF;
    totalQuantity += order.quantity;

    if (head == 0xFF) 
    {
        head = tail = newIndex;
    } 
    else 
    {
        orders[tail].levelNext = newIndex;
        orders[newIndex].levelPrev = tail;
        tail = newIndex;
    }
    return newIndex;
}

void PriceLevel::cancelOrder(uint8_t slotIndex)
{
    uint8_t prevIndex = orders[slotIndex].levelPrev;
    uint8_t nextIndex = orders[slotIndex].levelNext;
    stackTop--;
    freeIndexes[stackTop] = slotIndex; 
    totalQuantity -= orders[slotIndex].quantity;
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

FillResult PriceLevel::fillOrder(const ClientOrder& order, uint32_t levelPrice, uint8_t side, GlobalOrderInfo* infos)
{
    uint8_t current = head;
    uint16_t remaining = order.quantity;
    FillResult result;
    result.eventCount = 0;
    result.remaining = {order.price, remaining, order.orderID, order.type};

    while (current != 0xFF && remaining > 0)
    {
        EngineOrder& node = orders[current];
        uint8_t next = node.levelNext;

        uint16_t fill = (remaining < node.quantity) ? remaining : node.quantity;

        node.quantity -= fill;
        remaining -= fill;
        totalQuantity -= fill;

        if (result.eventCount < 64) [[likely]]
        {
            result.events[result.eventCount++] = {levelPrice, fill, node.orderID, 1, side, node.quantity == 0};
        }
        if (result.eventCount < 64) [[likely]]
        {
            result.events[result.eventCount++] = {levelPrice, fill, order.orderID, 1, (uint8_t)(side^1u), remaining == 0};
        }

        if (node.quantity == 0)
        {
            // mark filled inline while cache line is hot
            infos[node.orderID].live = false;

            uint8_t prev = node.levelPrev;

            if (prev == 0xFF)
                head = next;
            else
                orders[prev].levelNext = next;

            if (next == 0xFF)
                tail = prev;
            else
                orders[next].levelPrev = prev;

            freeIndexes[--stackTop] = current;
        }

        current = next;
    }
    result.remaining.quantity = remaining;
    return result;
}