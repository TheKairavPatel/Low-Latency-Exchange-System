#pragma once
#include <atomic>
#include "../engine/engine.hpp"

class Gateway
{
    Engine& engine; // gateway needs a reference to a engine object
    uint16_t freeIDs[65536]; // list of free engine IDs to use
    uint16_t topID; // stack pointer for free id list
    std::atomic<bool>& running; // global running variable 
    uint32_t extID[65536]; // maps internal ID to external ID for logging
    bool logging; // logs file in txt file if on (KEEP OFF FOR LARGE RUNS)
    uint32_t totalOrders; // how many orders gateway should simulate & send to engine
    uint32_t targetRate; // number of orders per sec to send to engine 

    bool isEmpty() { return topID == 65535; } // returns whether there are no more free engine IDs

    public:
    Gateway(Engine& engine, std::atomic<bool> &running, bool logging = true, uint32_t totalOrders = 500'000, uint32_t targetRate = 1000);
    void run(); // main run function
    uint16_t getID(); // returns a new unused engine id 
    void releaseID(uint16_t id); // stores a engine id back into free id list
    ClientOrder generateRandomOrder(); // returns new generated order (in real life would have to read from network)
    void registerOrder(uint16_t id, uint8_t type); // used for order/cancel simulation
};