#include "Queues.hpp"
#include <stdio.h>

bool OutboundQueue::push(const Event& event)
{
    //printf("pushing");
    uint16_t t = (tail.load(std::memory_order_relaxed));
    uint16_t nexttail = (t+1)&0x0FFF;
    if ((nexttail == head.load(std::memory_order_acquire)))
        return false;
    events[t] = event;
    tail.store(nexttail, std::memory_order_release);
    return true;
}

bool OutboundQueue::pop(Event& event)
{
    uint16_t h = (head.load(std::memory_order_relaxed));
    if (h == tail.load(std::memory_order_acquire))
        return false;
    event = events[h];
    head.store((h+1)&0x0FFF, std::memory_order_release);
    return true;
}

bool InboundQueue::push(const ClientOrder& order)
{
    uint16_t t = (tail.load(std::memory_order_relaxed));
    uint16_t nexttail = (t+1)&0x0FFF;
    if ((nexttail == head.load(std::memory_order_acquire)))
        return false;
    orders[t] = order;
    tail.store(nexttail, std::memory_order_release);
    return true;
}

bool InboundQueue::pop(ClientOrder& order)
{
    uint16_t h = (head.load(std::memory_order_relaxed));
    if (h == tail.load(std::memory_order_acquire))
        return false;
    order = orders[h];
    head.store((h+1)&0x0FFF, std::memory_order_release);
    return true;
}