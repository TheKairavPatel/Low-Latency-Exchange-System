#include "Gateway.hpp"
#include <cstdio>
#include <random>
#include <vector>
#include <algorithm>
#include <cstdio>
#include <x86intrin.h>
#include <algorithm>
#include <thread>
#include <chrono>
#include <set>

Gateway::Gateway(Engine& engine, std::atomic<bool> &running) : engine(engine), running(running)
{
    topID = 0;
    for (uint16_t i = 0; i < 65535; i++)
    {
        freeIDs[i] = i;
    }
}

uint16_t Gateway::getID()
{
    if (isEmpty()) [[unlikely]]
    {
        return 0xFFFF;
    }
    topID++;
    return freeIDs[topID - 1];
}

void Gateway::releaseID(uint16_t id)
{
    topID--;
    freeIDs[topID] = id;
    return;
}

ClientOrder Gateway::generateRandomOrder(uint16_t id)
{
    static std::mt19937 rng(std::random_device{}());
    static std::normal_distribution<double> bidOffDist(15.0, 5.0);
    static std::normal_distribution<double> askOffDist(15.0, 5.0);
    static std::uniform_int_distribution<uint32_t> aggOffDist(1, 5);
    static std::uniform_int_distribution<uint32_t> qtyDist(100, 500);
    static std::uniform_int_distribution<uint32_t> mktQtyDist(100, 500);
    static std::discrete_distribution<int> opDist({40, 40, 10, 10, 1, 1});
    static std::normal_distribution<double> trendStep(0.0, 0.1);

    static constexpr uint32_t CENTER = 74200;
    static int trend = 0;
    static constexpr int MAX_TREND = 300;

    int step = (int)std::round(trendStep(rng));
    trend = std::clamp(trend + step, -MAX_TREND, MAX_TREND);

    int bidOff = std::clamp((int)bidOffDist(rng), 1, 500);
    int askOff = std::clamp((int)askOffDist(rng), 1, 500);

    int op = opDist(rng);
    uint32_t price = 0;
    uint8_t type = 0;
    uint32_t qty = qtyDist(rng);

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
            qty = mktQtyDist(rng);
            break;
        case 5:
            type = 4;
            qty = mktQtyDist(rng);
            break;
    }
    return {price, qty, id, type};
}

void Gateway::run()
{
    static constexpr int TOTAL_ORDERS = 500000;
    static constexpr int TARGET_RATE  = 30000;
    static constexpr uint64_t NS_PER_ORDER = 1'000'000'000ULL / TARGET_RATE;

    FILE* logFile = fopen("eventslog.txt", "w");
    fprintf(logFile, "orderID,extID,price,quantity,type,side,fullyFilled\n");

    std::vector<uint16_t> liveBids, liveAsks;
    liveBids.reserve(65536);
    liveAsks.reserve(65536);

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
    std::uniform_int_distribution<int> cancelRoll(0, 9);

    int ordersPlaced = 0;

    auto nsNow = []() -> uint64_t {
        struct timespec ts;
        clock_gettime(CLOCK_MONOTONIC, &ts);
        return ts.tv_sec * 1'000'000'000ULL + ts.tv_nsec;
    };

    uint64_t nextSend = nsNow();

    while (ordersPlaced < TOTAL_ORDERS)
    {
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

        if (nsNow() < nextSend) continue;
        nextSend += NS_PER_ORDER;

        bool doCancel = false;
        bool cancelSide = cancelSideDist(rng);
        if (cancelSide == 0 && liveBids.size() > 10)
            doCancel = true;
        else if (cancelSide == 1 && liveAsks.size() > 10)
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
        else
        {
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
    printf("events logged to eventslog.txt\n");
    running.store(false, std::memory_order_relaxed);
}