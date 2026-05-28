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