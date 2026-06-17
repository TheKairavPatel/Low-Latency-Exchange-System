#include "Queues.hpp"

bool OutboundQueue::push(const Event& event)
{
    uint16_t t = tail.load(std::memory_order_relaxed); // producer tail index
    uint16_t nexttail = (t + 1) & 0x0FFF; // ring wrap (4096 size mask)

    if (nexttail == cachedHead) // check local cached head first (fast path)
    {
        cachedHead = head.load(std::memory_order_acquire); // refresh real head
        if (nexttail == cachedHead) // still full after refresh
            return false; // queue full
    }

    events[t] = event; // write event into ring slot
    tail.store(nexttail, std::memory_order_release); // publish new tail
    return true;
}

bool OutboundQueue::pop(Event& event)
{
    uint16_t h = head.load(std::memory_order_relaxed); // consumer head index

    if (h == cachedTail) // check cached tail first (empty fast path)
    {
        cachedTail = tail.load(std::memory_order_acquire); // refresh producer tail
        if (h == cachedTail) // still empty after refresh
            return false; // nothing to pop
    }

    event = events[h]; // read event from ring buffer
    head.store((h + 1) & 0x0FFF, std::memory_order_release); // advance head
    return true;
}

bool InboundQueue::push(const ClientOrder& order)
{
    uint16_t t = tail.load(std::memory_order_relaxed); // producer side (gateway)
    uint16_t nexttail = (t + 1) & 0x0FFF; // wrap index

    if (nexttail == cachedHead) // fast path full check
    {
        cachedHead = head.load(std::memory_order_acquire); // refresh consumer head
        if (nexttail == cachedHead) // confirmed full
            return false; // drop order
    }

    orders[t] = order; // write order into ring slot
    tail.store(nexttail, std::memory_order_release); // publish order
    return true;
}

bool InboundQueue::pop(ClientOrder& order)
{
    uint16_t h = head.load(std::memory_order_relaxed); // engine read side

    if (h == cachedTail) // fast empty check
    {
        cachedTail = tail.load(std::memory_order_acquire); // refresh producer tail
        if (h == cachedTail) // still empty
            return false; // no orders
    }

    order = orders[h]; // read next order
    head.store((h + 1) & 0x0FFF, std::memory_order_release); // advance consumer
    return true;
}