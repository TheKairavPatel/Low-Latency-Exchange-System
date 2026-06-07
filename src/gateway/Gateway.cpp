#include "Gateway.hpp"
#include <cstdio>
#include <random>
#include <vector>
#include <algorithm>
#include <chrono>

// The Gateway class simulates a client interface that generates random orders and sends them to the Engine, while also processing events coming back from the Engine.

// Set up free ID list and set free pointer to index 0 (indicating all IDs are free)
Gateway::Gateway(Engine& engine, std::atomic<bool> &running, bool logging, uint32_t qtysim) : engine(engine), running(running), logging(logging), totalOrders(qtysim)
{
    topID = 0;
    for (uint16_t i = 0; i < 65535; i++)
    {
        freeIDs[i] = i;
    }
}

// Returns a unique engine ID for a new order, or 0xFFFF if no IDs are available
uint16_t Gateway::getID()
{
    if (isEmpty()) [[unlikely]]
    {
        return 0xFFFF;
    }
    return freeIDs[topID++];
}

// Returns an ID to the pool of available IDs
void Gateway::releaseID(uint16_t id)
{
    topID--; // topID always points to the next free slot, so we decrement first to get the correct index
    freeIDs[topID] = id;
    return;
}

ClientOrder Gateway::generateRandomOrder(uint16_t id)
{
    static std::mt19937 rng(std::random_device{}());
    static std::normal_distribution<double> bidOffDist(3.0, 1.0);
    static std::normal_distribution<double> askOffDist(3.0, 1.0);
    static std::uniform_int_distribution<uint32_t> aggOffDist(1, 3);
    static std::lognormal_distribution<double> qtyDist(3.0, 0.7);
    static std::lognormal_distribution<double> mktQtyDist(2.5, 0.5);
    static std::discrete_distribution<int> opDist({40, 40, 10, 10, 1, 1});
    static std::uniform_int_distribution<int> trendStep(-1, 1);

    static constexpr uint32_t CENTER = 74200;
    static int trend = 0;
    static constexpr int MAX_TREND = 150;

    int step = trendStep(rng);
    trend = std::clamp(trend + step, -MAX_TREND, MAX_TREND);

    int bidOff = std::clamp((int)bidOffDist(rng), 1, 20);
    int askOff = std::clamp((int)askOffDist(rng), 1, 20);

    int op = opDist(rng);
    uint32_t price = 0;
    uint8_t type = 0;
    uint16_t qty = (uint16_t)std::clamp((int)qtyDist(rng), 5, 500);

    switch (op)
    {
        case 0:
            price = (uint32_t)std::clamp((int)CENTER - bidOff + trend, 74000, 76047);
            type = 0;
            break;
        case 1:
            price = (uint32_t)std::clamp((int)CENTER + askOff + trend, 74000, 76047);
            type = 1;
            break;
        case 2:
            price = (uint32_t)std::clamp((int)CENTER + askOff + (int)aggOffDist(rng) + trend, 74000, 76047);
            type = 0;
            break;
        case 3:
            price = (uint32_t)std::clamp((int)CENTER - bidOff - (int)aggOffDist(rng) + trend, 74000, 76047);
            type = 1;
            break;
        case 4:
            type = 3;
            qty = (uint16_t)std::clamp((int)mktQtyDist(rng), 5, 200);
            break;
        case 5:
            type = 4;
            qty = (uint16_t)std::clamp((int)mktQtyDist(rng), 5, 200);
            break;
    }
    return {price, qty, id, type};
}

void Gateway::run()
{
    // Setup for order generation and flow
    static constexpr int TARGET_RATE  = 100000;
    static constexpr uint64_t NS_PER_ORDER = 1'000'000'000ULL / TARGET_RATE;

    // Open log file if logging enabled
    FILE* logFile = nullptr;
    if (logging)
    {
        logFile = fopen("logs/eventslog.txt", "w");
        fprintf(logFile, "orderID,extID,price,quantity,type,side,fullyFilled\n");
    }

    // Vectors to track live orders for cancellation
    std::vector<uint16_t> liveBids, liveAsks;
    liveBids.reserve(65536);
    liveAsks.reserve(65536);

    // Helper to remove an ID from a vector (used for cancellations)
    auto removeID = [&](std::vector<uint16_t>& v, uint16_t id) {
        for (size_t i = 0; i < v.size(); i++) {
            if (v[i] == id) {
                v[i] = v.back();
                v.pop_back();
                return;
            }
        }
    };

    std::mt19937 rng(std::random_device{}());
    std::uniform_int_distribution<uint32_t> extIDDist(0, UINT32_MAX);
    std::uniform_int_distribution<int> cancelSideDist(0, 1);
    std::uniform_int_distribution<int> cancelRoll(0, 1);

    uint32_t ordersPlaced = 0;

    // Lambda to get current time in nanoseconds
    auto nsNow = []() -> uint64_t {
    #ifdef _WIN32
        using namespace std::chrono;
        return duration_cast<nanoseconds>(steady_clock::now().time_since_epoch()).count();
    #else
        struct timespec ts;
        clock_gettime(CLOCK_MONOTONIC, &ts);
        return ts.tv_sec * 1'000'000'000ULL + ts.tv_nsec;
    #endif
    };

    uint64_t nextSend = nsNow();

    // Main loop: process outbound events and send new orders at the target rate
    while (ordersPlaced < totalOrders)
    {
        // Drain outbound events from engine
        Event e;
        while (engine.outboundQueue.pop(e))
        {
            if (logging)
                fprintf(logFile, "%u,%u,%.2f,%u,%u,%u,%d\n",
                        e.orderID, extID[e.orderID], e.price / 100.0f, e.quantity, e.type, e.side, e.fullyFilled);
            if (e.fullyFilled)
            {
                releaseID(e.orderID);
                if (e.side == 0) removeID(liveBids, e.orderID);
                else             removeID(liveAsks, e.orderID);
            }
        }

        if (nsNow() < nextSend) continue;
        nextSend += NS_PER_ORDER;

        // Attempt a cancel independently of order placement
        bool doCancel = false;
        bool cancelSide = cancelSideDist(rng);
        if (cancelSide == 0 && liveBids.size() > 0)
            doCancel = true;
        else if (cancelSide == 1 && liveAsks.size() > 0)
            doCancel = true;
        if (cancelRoll(rng) != 0) doCancel = false;

        if (doCancel)
        {
            std::vector<uint16_t>& side = (cancelSide == 0) ? liveBids : liveAsks;
            std::uniform_int_distribution<size_t> idxDist(0, side.size() - 1);
            size_t idx = idxDist(rng);
            uint16_t cancelID = side[idx];
            side[idx] = side.back();
            side.pop_back();
            ClientOrder cancel = {0, 0, cancelID, 2};
            engine.inboundQueue.push(cancel);
        }

        // Always place a new order every tick
        uint16_t id = getID();
        if (id == 0xFFFF) continue;

        if (logging) extID[id] = extIDDist(rng);

        ClientOrder order = generateRandomOrder(id);

        if (order.type == 0) liveBids.push_back(id);
        else if (order.type == 1) liveAsks.push_back(id);

        engine.inboundQueue.push(order);
        ordersPlaced++;
    }

    // Drain remaining events
    Event e;
    while (engine.outboundQueue.pop(e))
    {
        if (logging)
            fprintf(logFile, "%u,%u,%.2f,%u,%u,%u,%d\n",
                    e.orderID, extID[e.orderID], e.price / 100.0f, e.quantity, e.type, e.side, e.fullyFilled);
        if (e.fullyFilled)
        {
            releaseID(e.orderID);
            if (e.side == 0) removeID(liveBids, e.orderID);
            else             removeID(liveAsks, e.orderID);
        }
    }

    if (logging)
    {
        fclose(logFile);
        printf("events logged to logs/eventslog.txt\n");
    }

    running.store(false, std::memory_order_relaxed);
}