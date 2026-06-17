#include "SnapshotWriter.hpp"
#include <cstdio>
#include <cstring>
#include <thread>
#include <chrono>

void SnapshotWriter::writeSnapshot(const char* filename)
{
    FILE* f = fopen(filename, "w");
    if (!f) return; // fail silently if file can't open

    fprintf(f, "{\n  \"bids\": [\n"); // start json output (bids section)

    uint16_t bestBid = engine.getBestBid(); // current top of book (bid side)
    int bidCount = 0;

    for (int i = 0; i < 10 && bidCount < 10; i++) // scan down bid side (max 10 levels)
    {
        if (bestBid < i) break; // safety check if we underflow levels

        uint16_t level = bestBid - i; // walk down price levels
        uint32_t qty = engine.buyLevels[level].totalQuantity; // total size at level

        if (qty == 0) continue; // skip empty levels

        if (bidCount > 0)
            fprintf(f, ",\n"); // json comma formatting

        fprintf(f, "    {\"price\": %u, \"qty\": %u}",
                engine.basePrice + level, qty); // output bid level

        bidCount++; // only count non-empty levels
    }

    fprintf(f, "\n  ],\n  \"asks\": [\n"); // switch to ask side

    uint16_t bestAsk = engine.getBestAsk(); // current best ask
    int askCount = 0;

    for (int i = 0; i < 10 && askCount < 10; i++) // scan up ask side
    {
        uint16_t level = bestAsk + i; // walk upward in price

        if (level >= LEVELS) break; // prevent out-of-bounds

        uint32_t qty = engine.sellLevels[level].totalQuantity; // size at level

        if (qty == 0) continue; // skip empty levels

        if (askCount > 0)
            fprintf(f, ",\n"); // json formatting

        fprintf(f, "    {\"price\": %u, \"qty\": %u}",
                engine.basePrice + level, qty); // output ask level

        askCount++; // only count non-empty levels
    }

    fprintf(f, "\n  ],\n  \"totalOrders\": %llu\n}\n",
            (unsigned long long)engine.totalOrders); // dump total engine activity

    fclose(f); // close snapshot file
}

void SnapshotWriter::run()
{
    while (running.load(std::memory_order_relaxed)) // main snapshot loop
    {
        writeSnapshot("frontend/snapshot.json"); // overwrite latest snapshot

        for (int i = 0; i < 10; i++) // small sleep loop so we can exit fast
        {
            if (!running.load(std::memory_order_relaxed))
                return; // early exit if shutdown triggered

            std::this_thread::sleep_for(std::chrono::milliseconds(50)); // 500ms total sleep
        }
    }
}