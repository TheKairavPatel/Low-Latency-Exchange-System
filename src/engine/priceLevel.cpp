#include "priceLevel.hpp"

PriceLevel::PriceLevel()
{
    stackTop = 0; // stack is empty when stackTop is 0, and full when stackTop is 255
    head = 0xFF; // original value is 255, which indicates empty
    tail = 0xFF; // original value is 255, which indicates empty
    for (int i = 0; i < 255; i++) 
    {
        freeIndexes[i] = (uint8_t)i; // initialize freeIndexes stack with all indexes from 0 to 254
    }
    totalQuantity = 0; // starting with 0 quantity at this price level
}

uint8_t PriceLevel::insertOrder(const ClientOrder& order, GlobalOrderInfo* infos)
{
    if (stackTop == 255) [[unlikely]]
    {
        return 0xFF; // return sentinel value 255 is price level is full (no slots left)
    }
    uint8_t newIndex = freeIndexes[stackTop]; // get the next free index from the stack
    stackTop++; // increment stacktop to next free index
    infos[order.orderID].live = true;

    // insert the new order into the orders array at newIndex
    orders[newIndex].quantity = order.quantity;
    orders[newIndex].orderID = order.orderID;
    orders[newIndex].levelNext = 0xFF;
    orders[newIndex].levelPrev = 0xFF;

    totalQuantity += order.quantity; // update total quantity at this price level

    if (head == 0xFF) // if the price level is currently empty, set head and tail to the new index
    {
        head = tail = newIndex;
    } 
    else 
    {
        orders[tail].levelNext = newIndex; // link the new order to the end of the linked list
        orders[newIndex].levelPrev = tail; // link the new order back to the previous tail
        tail = newIndex; // update tail to the new index
    }
    return newIndex; // return the index of the newly inserted order
}

void PriceLevel::cancelOrder(uint8_t slotIndex)
{
    uint8_t prevIndex = orders[slotIndex].levelPrev; // get previous and next indexes
    uint8_t nextIndex = orders[slotIndex].levelNext;
    stackTop--; // decrement stackTop to add the freed index back to the stack
    freeIndexes[stackTop] = slotIndex;  // add freed index
    totalQuantity -= orders[slotIndex].quantity; // subtract cancelled order quantity
    if (prevIndex == 0xFF)
    {
        head = nextIndex; // if there is no previous order, update head to the next index
    }
    else
    {
        orders[prevIndex].levelNext = nextIndex; // link previous order to the next order, skipping the cancelled order
    }
    if (nextIndex == 0xFF)
    {
        tail = prevIndex; // if there is no next order, update tail to the previous index
    }
    else
    {
        orders[nextIndex].levelPrev = prevIndex; // link next order back to the previous order, skipping the cancelled order
    }
}

FillResult PriceLevel::fillOrder(const ClientOrder& order, uint32_t levelPrice, uint8_t side, GlobalOrderInfo* infos)
{
    uint8_t current = head; // start filling from head (oldest order)
    uint16_t remaining = order.quantity; // track remaining quantity left to fill
    FillResult result; // initialize a FillResult struct to store the remaining order and generated events
    result.eventCount = 0; // counter for events generated during the filling process
    result.remaining = {order.price, remaining, order.orderID, order.type}; // initialize remaining order in the result with the incoming order's details for now

    while (current != 0xFF && remaining > 0) // keep looping until no more orders to fill, or no more quantity to fill
    {
        EngineOrder& node = orders[current]; // reference to current order to fill against
        uint8_t next = node.levelNext; // save next index before potentially modifying the linked list
        uint16_t fill = (remaining < node.quantity) ? remaining : node.quantity; // calculate fill quantity, which is the minimum of remaining quantity and current node's quantity
        node.quantity -= fill; // reduce the quantity of the current node by the fill amount
        remaining -= fill; // reduce the remaining quantity by the fill amount
        totalQuantity -= fill; // update total quantity at this price level

        if (result.eventCount < 64) [[likely]]
        {
            // generate a fill event for the order in the price level that got filled
            result.events[result.eventCount++] = {levelPrice, fill, node.orderID, 1, side, node.quantity == 0};
        }
        if (result.eventCount < 64) [[likely]]
        {
            // generate a fill event for the incoming order that is being filled
            result.events[result.eventCount++] = {levelPrice, fill, order.orderID, 1, (uint8_t)(side^1u), remaining == 0};
        }

        if (node.quantity == 0) // if current node exhausted, remove it from the linked list and add its index back to the freeIndexes stack
        {
            infos[node.orderID].live = false; // mark the order as not live in the global order info table
            uint8_t prev = node.levelPrev; // get previous and next indexes before removing the current node

            if (prev == 0xFF) // if no previous, update head to next
                head = next;
            else
                orders[prev].levelNext = next; // otherwise, link previous node to next node, skipping the current node

            if (next == 0xFF) // if no next, tail becomes previous
                tail = prev;
            else
                orders[next].levelPrev = prev; // otherwise, link next node back to previous node, skipping the current node
            freeIndexes[--stackTop] = current; // add the current index back to the freeIndexes stack by decrementing stackTop and assigning it the freed index
        }

        current = next; // move to the next order in the linked list for the next iteration of filling
    }
    result.remaining.quantity = remaining; // update remaining quantity to reflect how much is left after it got filled
    return result; // return the FillResult containing the remaining order and any events generated during the filling process
}