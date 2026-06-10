#pragma once
#include <atomic>
#include "../engine/engine.hpp"

class Gateway
{
    Engine& engine;
    uint16_t freeIDs[65536];
    uint16_t topID;
    std::atomic<bool>& running;
    uint32_t extID[65536]; // maps internal ID to external ID for logging
    bool logging;
    uint32_t totalOrders;
    bool owned[65536];

    bool isEmpty() { return topID == 65535; }

    public:
    Gateway(Engine& engine, std::atomic<bool> &running, bool logging = true, uint32_t totalOrders = 500'000);
    void run();
    uint16_t getID();
    void releaseID(uint16_t id);
    ClientOrder generateRandomOrder();
    void registerOrder(uint16_t id, uint8_t type);
};