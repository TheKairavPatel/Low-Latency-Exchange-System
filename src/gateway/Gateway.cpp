#include "Gateway.hpp"
#include <cstdio>
#include <random>
#include <vector>
#include <algorithm>

static std::vector<uint16_t> liveBids;
static std::vector<uint16_t> liveAsks;

Gateway::Gateway(Engine& engine, std::atomic<bool> &running, bool logging, uint32_t qtysim) : engine(engine), running(running), logging(logging), totalOrders(qtysim)
{
    topID = 0;
    for (uint16_t i = 0; i < 65535; i++)
        freeIDs[i] = i;
    liveBids.reserve(65536);
    liveAsks.reserve(65536);
}

uint16_t Gateway::getID()
{
    if (isEmpty()) [[unlikely]]
        return 0xFFFF;
    return freeIDs[topID++];
}

void Gateway::releaseID(uint16_t id)
{
    freeIDs[--topID] = id;
}

void Gateway::registerOrder(uint16_t id, uint8_t type)
{
    if (type == 0) liveBids.push_back(id);
    else if (type == 1) liveAsks.push_back(id);
}

// Simulates receiving a raw client order — no engine ID assigned yet
ClientOrder Gateway::generateRandomOrder()
{
    static std::mt19937 rng(std::random_device{}());
    static std::normal_distribution<double>        bidOffDist(3.0, 1.0);
    static std::normal_distribution<double>        askOffDist(3.0, 1.0);
    static std::uniform_int_distribution<uint32_t> aggOffDist(1, 3);
    static std::lognormal_distribution<double>     qtyDist(3.0, 0.7);
    static std::lognormal_distribution<double>     mktQtyDist(2.5, 0.5);
    static std::discrete_distribution<int>         opDist({40, 40, 10, 10, 1, 1});
    static std::uniform_int_distribution<int>      trendStep(-1, 1);
    static std::uniform_int_distribution<int>      cancelSideDist(0, 1);
    static std::uniform_int_distribution<int>      cancelRoll(0, 2);

    static constexpr uint32_t CENTER    = 74200;
    static constexpr int      MAX_TREND = 150;
    static int trend = 0;

    // Attempt a cancel
    bool cancelSide = cancelSideDist(rng);
    std::vector<uint16_t>& cancelPool = cancelSide ? liveAsks : liveBids;
    if (cancelRoll(rng) == 0 && !cancelPool.empty())
    {
        std::uniform_int_distribution<size_t> idxDist(0, cancelPool.size() - 1);
        size_t idx = idxDist(rng);
        uint16_t cancelID = cancelPool[idx];
        cancelPool[idx] = cancelPool.back();
        cancelPool.pop_back();
        return {0, 0, cancelID, 2};
    }

    // Generate new order with no ID yet
    trend = std::clamp(trend + trendStep(rng), -MAX_TREND, MAX_TREND);
    int bidOff = std::clamp((int)bidOffDist(rng), 1, 20);
    int askOff = std::clamp((int)askOffDist(rng), 1, 20);
    int op     = opDist(rng);

    uint32_t price = 0;
    uint8_t  type  = 0;
    uint16_t qty   = (uint16_t)std::clamp((int)qtyDist(rng), 5, 500);

    switch (op)
    {
        case 0: price = (uint32_t)std::clamp((int)CENTER - bidOff + trend, 74000, 76047); type = 0; break;
        case 1: price = (uint32_t)std::clamp((int)CENTER + askOff + trend, 74000, 76047); type = 1; break;
        case 2: price = (uint32_t)std::clamp((int)CENTER + askOff + (int)aggOffDist(rng) + trend, 74000, 76047); type = 0; break;
        case 3: price = (uint32_t)std::clamp((int)CENTER - bidOff - (int)aggOffDist(rng) + trend, 74000, 76047); type = 1; break;
        case 4: type = 3; qty = (uint16_t)std::clamp((int)mktQtyDist(rng), 5, 200); break;
        case 5: type = 4; qty = (uint16_t)std::clamp((int)mktQtyDist(rng), 5, 200); break;
    }

    return {price, qty, 0xFFFF, type};
}

void Gateway::run()
{
    static constexpr int      TARGET_RATE  = 100'000'000;
    static constexpr uint64_t NS_PER_ORDER = 1'000'000'000ULL / TARGET_RATE;

    FILE* logFile = nullptr;
    if (logging)
    {
        logFile = fopen("logs/eventslog_pretty.txt", "w");
        setvbuf(logFile, nullptr, _IOFBF, 1024 * 1024); // 1MB buffer
    }

    std::mt19937 rng(std::random_device{}());
    std::uniform_int_distribution<uint32_t> extIDDist(0, UINT32_MAX);

    auto nsNow = []() -> uint64_t {
        struct timespec ts;
        clock_gettime(CLOCK_MONOTONIC, &ts);
        return ts.tv_sec * 1'000'000'000ULL + ts.tv_nsec;
    };

    uint64_t nextSend     = nsNow();
    uint32_t ordersPlaced = 0;

    while (ordersPlaced < totalOrders)
    {
        // Drain and log outbound events
        Event e;
        while (engine.outboundQueue.pop(e))
        {
            if (logging)
                {
                    const char* eventType;
                    if (e.type == 0)
                        eventType = "   CANCEL   ";
                    else if (e.type == 1 && e.price == 0 && e.fullyFilled)
                        eventType = e.quantity == 0 ? " MKT FILLED " : "MKT PARTIAL ";
                    else if (e.fullyFilled)
                        eventType = "    FILL    ";
                    else
                        eventType = "PARTIAL FILL";

                    const char* sideStr = (e.side == 0) ? "BUY " : "SELL";

                    if (e.type == 1 && e.price == 0)
                        fprintf(logFile, "[%s] | %s | EXT_ID: %-12u | ENG_ID: %-6u | REMAINING QTY: %u\n",
                                eventType, sideStr, extID[e.orderID], e.orderID, e.quantity);
                    else
                        fprintf(logFile, "[%s] | %s | EXT_ID: %-12u | ENG_ID: %-6u | $%.2f | QTY: %u\n",
                                eventType, sideStr, extID[e.orderID], e.orderID, e.price / 100.0f, e.quantity);
                }
            if (e.fullyFilled)
                releaseID(e.orderID);
        }

        if (nsNow() < nextSend) continue;
        nextSend += NS_PER_ORDER;

        // Receive order from sim
        ClientOrder order = generateRandomOrder();

        // Cancels already have an engine ID — dispatch directly
        if (order.type == 2)
        {
            engine.inboundQueue.push(order);
            ordersPlaced++;
            continue;
        }

        // Assign engine ID, register, dispatch
        uint16_t id = getID();
        if (id == 0xFFFF) continue;

        order.orderID = id;
        if (logging) extID[id] = extIDDist(rng);
        registerOrder(id, order.type);

        engine.inboundQueue.push(order);
        ordersPlaced++;
        if (ordersPlaced == 50000)
        {
            fclose(logFile);
            printf("events logged to logs/eventslog.txt\n");
            logging = false;
            logFile = nullptr;
        }
    }

    // Drain remaining events
    Event e;
    while (engine.outboundQueue.pop(e))
    {
        if (e.fullyFilled)
            releaseID(e.orderID);
    }

    if (logging)
    {
        fclose(logFile);
        printf("events logged to logs/eventslog.txt\n");
    }

    running.store(false, std::memory_order_relaxed);
}