#include "SnapshotWriter.hpp"
#include <cstdio>
#include <thread>

void SnapshotWriter::writeSnapshot(const char* filename, const BookSnapshot& snap)
{
    FILE* f = fopen(filename, "w");
    if (!f) return; // fail silently if file can't open

    fprintf(f, "{\n  \"bids\": [\n"); // start json output (bids section)

    for (int i = 0; i < snap.bidCount; i++) // write bid levels from snapshot
    {
        if (i > 0) fprintf(f, ",\n"); // json comma formatting
        fprintf(f, "    {\"price\": %u, \"qty\": %u}", snap.bids[i].price, snap.bids[i].qty);
    }

    fprintf(f, "\n  ],\n  \"asks\": [\n"); // switch to ask side

    for (int i = 0; i < snap.askCount; i++) // write ask levels from snapshot
    {
        if (i > 0) fprintf(f, ",\n"); // json comma formatting
        fprintf(f, "    {\"price\": %u, \"qty\": %u}", snap.asks[i].price, snap.asks[i].qty);
    }

    fprintf(f, "\n  ],\n  \"totalOrders\": %llu\n}\n",
            (unsigned long long)snap.totalOrders); // dump total engine activity

    fclose(f); // close snapshot file
}

void SnapshotWriter::run()
{
    BookSnapshot snap;
    BookSnapshot latest;
    bool hasSnapshot = false;

    while (running.load(std::memory_order_relaxed))
    {
        bool gotOne = false;
        while (engine.snapshotQueue.pop(snap))
        {
            latest = snap;
            gotOne = true;
        }

        if (gotOne) hasSnapshot = true;

        if (hasSnapshot)
            writeSnapshot("frontend/snapshot.json", latest);

        for (int i = 0; i < 10; i++)
        {
            if (!running.load(std::memory_order_relaxed)) return;
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    }
}