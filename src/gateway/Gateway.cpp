#include "Gateway.hpp"
#include <cstdio>
#include <random>
#include <vector>
#include <algorithm>
#include <chrono>

// The Gateway class simulates a client interface that generates random orders and sends them to the Engine, while also processing events coming back from the Engine.

// Set up free ID list and set free pointer to index 0 (indicating all IDs are free)
Gateway::Gateway(Engine& engine, std::atomic<bool> &running) : engine(engine), running(running)
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
    static std::normal_distribution<double> bidOffDist(15.0, 5.0);
    static std::normal_distribution<double> askOffDist(15.0, 5.0);
    static std::uniform_int_distribution<uint32_t> aggOffDist(1, 5);
    static std::lognormal_distribution<double> qtyDist(3.8, 1.0);
    static std::lognormal_distribution<double> mktQtyDist(3.8, 1.0);
    static std::discrete_distribution<int> opDist({40, 40, 10, 10, 1, 1});
    static std::uniform_int_distribution<int> trendStep(-1, 1);

    static constexpr uint32_t CENTER = 74200;
    static int trend = 0;
    static constexpr int MAX_TREND = 150;

    int step = trendStep(rng);
    trend = std::clamp(trend + step, -MAX_TREND, MAX_TREND);

    int bidOff = std::clamp((int)bidOffDist(rng), 1, 500);
    int askOff = std::clamp((int)askOffDist(rng), 1, 500);

    int op = opDist(rng);
    uint32_t price = 0;
    uint8_t type = 0;
    uint16_t qty = (uint16_t)std::clamp((int)qtyDist(rng), 5, 2000);

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
            qty = (uint16_t)std::clamp((int)mktQtyDist(rng), 5, 2000);
            break;
        case 5:
            type = 4;
            qty = (uint16_t)std::clamp((int)mktQtyDist(rng), 5, 2000);
            break;
    }
    return {price, qty, id, type};
}

void Gateway::run()
{
    // Setup for order generation and flow
    static constexpr int TOTAL_ORDERS = 500000;
    static constexpr int TARGET_RATE  = 100000;
    static constexpr uint64_t NS_PER_ORDER = 1'000'000'000ULL / TARGET_RATE;

    // Open log file and write header
    FILE* logFile = fopen("logs/eventslog.txt", "w");
    fprintf(logFile, "orderID,extID,price,quantity,type,side,fullyFilled\n");

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

    // 
    std::mt19937 rng(std::random_device{}());
    std::uniform_int_distribution<uint32_t> extIDDist(0, UINT32_MAX);
    std::uniform_int_distribution<int> cancelSideDist(0, 1);
    std::uniform_int_distribution<int> cancelRoll(0, 9);

    // Order count
    int ordersPlaced = 0;

    // Lambda to get current time in nanoseconds
    auto nsNow = []() -> uint64_t 
    {
        using namespace std::chrono;
        return duration_cast<nanoseconds>(steady_clock::now().time_since_epoch()).count();
    };

    // Initial timestamp for sending orders
    uint64_t nextSend = nsNow();

    // Main loop: process outbound events and send new orders at the target rate, with some random cancellations
    while (ordersPlaced < TOTAL_ORDERS)
    {
        // Create event struct to hold popped events from the engine's outbound queue
        Event e;
        while (engine.outboundQueue.pop(e))
        {
            // Log the event to the file
            fprintf(logFile, "%u,%u,%u,%u,%u,%u,%d\n",
                    e.orderID, extID[e.orderID], e.price, e.quantity, e.type, e.side, e.fullyFilled);
            // If the event indicates the order was fully filled, release the ID and remove it from live tracking
            if (e.fullyFilled)
            {
                releaseID(e.orderID);
                if (e.side == 0) removeID(liveBids, e.orderID);
                else             removeID(liveAsks, e.orderID);
            }
        }

        // Check if it's time to send the next order
        if (nsNow() < nextSend) continue;
        nextSend += NS_PER_ORDER;

        // Randomly decide whether to send a new order or cancel an existing one, with cancellations being less frequent
        bool doCancel = false;
        bool cancelSide = cancelSideDist(rng);
        if (cancelSide == 0 && liveBids.size() > 10)
            doCancel = true;
        else if (cancelSide == 1 && liveAsks.size() > 10)
            doCancel = true;
        if (cancelRoll(rng) != 0) doCancel = false;

        if (doCancel)
        {
            // Choose a random order from the appropriate side to cancel
            std::vector<uint16_t>& side = (cancelSide == 0) ? liveBids : liveAsks;
            std::uniform_int_distribution<size_t> idxDist(0, side.size() - 1);
            size_t idx = idxDist(rng);
            uint16_t cancelID = side[idx];
            side[idx] = side.back();
            side.pop_back();

            ClientOrder cancel = {0, 0, cancelID, 2};
            engine.inboundQueue.push(cancel);
        }
        else
        {
            // Generate a new order and send it to the engine, unless no IDs are available
            uint16_t id = getID();
            if (id == 0xFFFF) continue;

            extID[id] = extIDDist(rng);

            ClientOrder order = generateRandomOrder(id);

            if (order.type == 0) liveBids.push_back(id);
            else if (order.type == 1) liveAsks.push_back(id);

            engine.inboundQueue.push(order);
            ordersPlaced++;
        }
    }

    // After main loop, continue processing outbound events until the engine is done
    Event e;
    while (engine.outboundQueue.pop(e))
    {
        fprintf(logFile, "%u,%u,%u,%u,%u,%u,%d\n",
                e.orderID, extID[e.orderID], e.price, e.quantity, e.type, e.side, e.fullyFilled);
        if (e.fullyFilled)
        {
            releaseID(e.orderID);
            if (e.side == 0) removeID(liveBids, e.orderID);
            else             removeID(liveAsks, e.orderID);
        }
    }
    fclose(logFile);
    printf("events logged to build\\eventslog.txt\n");
    running.store(false, std::memory_order_relaxed); // signal engine thread to stop spinning
}