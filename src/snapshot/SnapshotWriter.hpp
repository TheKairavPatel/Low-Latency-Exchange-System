#pragma once
#include <atomic>
#include "../engine/engine.hpp"

class SnapshotWriter
{
    Engine& engine;
    std::atomic<bool>& running;

    public:
    SnapshotWriter(Engine& engine, std::atomic<bool>& running) : engine(engine), running(running) {}
    void writeSnapshot(const char* filename);
    void run();
};