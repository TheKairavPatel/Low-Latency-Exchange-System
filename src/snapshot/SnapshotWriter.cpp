#include "SnapshotWriter.hpp"
#include <cstdio>
#include <cstring>
#include <thread>
#include <chrono>

void SnapshotWriter::writeSnapshot(const char* filename)
{
    FILE* f = fopen(filename, "w");
    if (!f) return;

    fprintf(f, "{\n  \"bids\": [\n");

    uint16_t bestBid = engine.getBestBid();
    int bidCount = 0;
    for (int i = 0; i < 10 && bidCount < 10; i++)
    {
        if (bestBid < i) break;
        uint16_t level = bestBid - i;
        uint32_t qty = engine.buyLevels[level].totalQuantity;
        if (qty == 0) continue;
        if (bidCount > 0) fprintf(f, ",\n");
        fprintf(f, "    {\"price\": %u, \"qty\": %u}",
                engine.basePrice + level, qty);
        bidCount++;
    }

    fprintf(f, "\n  ],\n  \"asks\": [\n");

    uint16_t bestAsk = engine.getBestAsk();
    int askCount = 0;
    for (int i = 0; i < 10 && askCount < 10; i++)
    {
        uint16_t level = bestAsk + i;
        if (level >= LEVELS) break;
        uint32_t qty = engine.sellLevels[level].totalQuantity;
        if (qty == 0) continue;
        if (askCount > 0) fprintf(f, ",\n");
        fprintf(f, "    {\"price\": %u, \"qty\": %u}",
                engine.basePrice + level, qty);
        askCount++;
    }

    fprintf(f, "\n  ]\n}\n");
    fclose(f);
}

void SnapshotWriter::run()
{
    while (running.load(std::memory_order_relaxed))
    {
        writeSnapshot("frontend/snapshot.json");
        for (int i = 0; i < 10; i++)
        {
            if (!running.load(std::memory_order_relaxed)) return;
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
        }
    }
}