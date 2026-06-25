#pragma once
#include <atomic>
#include "../engine/order.hpp"

class OutboundQueue
{
    alignas(64) Event events[4096]; // ring buffer for engine -> gateway events
    alignas(64) std::atomic<uint16_t> head; // consumer side index
    alignas(64) std::atomic<uint16_t> tail; // producer side index

    alignas(64) uint16_t cachedHead{0}; // local cached head to reduce atomics
    alignas(64) uint16_t cachedTail{0}; // local cached tail to reduce atomics

    public:
    OutboundQueue() : head(0), tail(0) {} // start empty queue

    bool push(const Event& event); // engine writes events here
    bool pop(Event& event); // gateway reads events here
};

class InboundQueue
{
    alignas(64) ClientOrder orders[4096]; // ring buffer for gateway -> engine orders
    alignas(64) std::atomic<uint16_t> head; // consumer index (engine side)
    alignas(64) std::atomic<uint16_t> tail; // producer index (gateway side)

    alignas(64) uint16_t cachedHead{0}; // cached head for faster pops
    alignas(64) uint16_t cachedTail{0}; // cached tail for faster pushes

    public:
    InboundQueue() : head(0), tail(0) {} // initialize empty queue

    bool push(const ClientOrder& order); // gateway pushes new orders
    bool pop(ClientOrder& order); // engine consumes orders
};

class SnapshotQueue
{
    alignas(64) BookSnapshot snapshots[256];
    alignas(64) std::atomic<uint8_t> head;
    alignas(64) std::atomic<uint8_t> tail;

    alignas(64) uint8_t cachedHead{0};
    alignas(64) uint8_t cachedTail{0};

    public:
    SnapshotQueue() : head(0), tail(0) {}

    bool push(const BookSnapshot& snapshot);
    bool pop(BookSnapshot& snapshot);
};