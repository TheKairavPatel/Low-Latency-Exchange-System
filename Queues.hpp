#include "order.hpp"
#include <atomic>
#include <cstdint>

class OutboundQueue
{
    alignas(64) Event events[4096];
    alignas(64) std::atomic<uint16_t> head;
    alignas(64) std::atomic<uint16_t> tail;

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

    public:
    InboundQueue() : head(0), tail(0) {}
    bool push(const ClientOrder& order);
    bool pop(ClientOrder& order);
};