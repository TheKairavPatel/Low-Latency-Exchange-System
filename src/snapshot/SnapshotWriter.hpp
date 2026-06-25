#pragma once
#include <atomic>
#include "../engine/engine.hpp"
#include <chrono>

class SnapshotWriter
{
    Engine& engine; // reference to live engine state (read-only snapshot source)
    std::atomic<bool>& running; // global stop flag shared with engine/gateway

    public:
    SnapshotWriter(Engine& engine, std::atomic<bool>& running)
    : engine(engine), running(running) {} // bind engine + running flag

    void writeSnapshot(const char* filename, const BookSnapshot& snap); // dump full orderbook state to json
    void run(); // main loop that keeps snapshotting engine state
};