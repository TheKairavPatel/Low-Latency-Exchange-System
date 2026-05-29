#pragma once
#include "engine.hpp"

class Gateway
{
    Engine& engine;
    uint16_t freeIDs[65536];
    uint16_t topID;
    std::atomic<bool>& running;


    bool isEmpty() { return topID == 65535; }

    public:
    Gateway(Engine& engine, std::atomic<bool> &running);
    void run();
    uint16_t getID();
    void releaseID(uint16_t id);
};