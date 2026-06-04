#pragma once
#include "order.hpp"
#include <atomic>
#include <cstdint>

class OutboundQueue
{
    alignas(64) Event events[4096];
    alignas(64) std::atomic<uint16_t> head;
    alignas(64) std::atomic<uint16_t> tail;
    alignas(64) uint16_t cachedHead{0};
    alignas(64) uint16_t cachedTail{0};

    public:
    OutboundQueue() : head(0), tail(0) {}
    bool push(const Event& event);
    bool pop(Event& event);
};

class InboundQueue
{
    alignas(64) ClientOrder orders[4096];
    alignas(64) std::atomic<uint16_t> head;
    alignas(64) std::atomic<uint16_t> tail;
    alignas(64) uint16_t cachedHead{0};
    alignas(64) uint16_t cachedTail{0};

    public:
    InboundQueue() : head(0), tail(0) {}
    bool push(const ClientOrder& order);
    bool pop(ClientOrder& order);
};