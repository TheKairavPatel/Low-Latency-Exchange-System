#include "Gateway.hpp"
#include <cstdio>
#include <random>
#include <vector>
#include <algorithm>

static std::vector<uint16_t> liveBids; // tracks active bid order IDs for cancel simulation
static std::vector<uint16_t> liveAsks; // tracks active ask order IDs for cancel simulation

Gateway::Gateway(Engine& engine, std::atomic<bool> &running, bool logging, uint32_t qtysim, uint32_t target)
: engine(engine), running(running), logging(logging), totalOrders(qtysim), targetRate(target)
{
    topID = 0; // stack pointer for free ID list starts at 0

    for (uint16_t i = 0; i < 65535; i++)
        freeIDs[i] = i; // preload all engine IDs into free list

    liveBids.reserve(65536); // avoid reallocs during sim
    liveAsks.reserve(65536); // same for asks
}

uint16_t Gateway::getID()
{
    if (isEmpty()) [[unlikely]] // no free IDs left
        return 0xFFFF;

    return freeIDs[topID++]; // pop ID from stack
}

void Gateway::releaseID(uint16_t id)
{
    freeIDs[--topID] = id; // push ID back into free stack
}

void Gateway::registerOrder(uint16_t id, uint8_t type)
{
    if (type == 0) liveBids.push_back(id); // track live bid order
    else if (type == 1) liveAsks.push_back(id); // track live ask order
}

// simulates raw incoming client order (no engine ID assigned yet)
ClientOrder Gateway::generateRandomOrder()
{
    static std::mt19937 rng(std::random_device{}()); // RNG for full sim randomness

    static std::normal_distribution<double> bidOffDist(3.0, 1.0); // bid skew distribution
    static std::normal_distribution<double> askOffDist(3.0, 1.0); // ask skew distribution

    static std::uniform_int_distribution<uint32_t> aggOffDist(1, 3); // aggression offset

    static std::lognormal_distribution<double> qtyDist(3.0, 0.7); // resting order size
    static std::lognormal_distribution<double> mktQtyDist(2.5, 0.5); // market order size

    static std::discrete_distribution<int> opDist({40, 40, 10, 10, 1, 1}); // order type weights

    static std::uniform_int_distribution<int> trendStep(-1, 1); // price drift
    static std::uniform_int_distribution<int> cancelSideDist(0, 1); // pick bid/ask cancel pool
    static std::uniform_int_distribution<int> cancelRoll(0, 2); // cancel probability

    static constexpr uint32_t CENTER = 74200; // mid price anchor
    static constexpr int MAX_TREND = 150; // bounds for drift
    static int trend = 0; // persistent market drift state

    // try cancel first (cheap way to simulate order churn)
    bool cancelSide = cancelSideDist(rng); // pick side
    std::vector<uint16_t>& cancelPool = cancelSide ? liveAsks : liveBids; // choose pool

    if (cancelRoll(rng) == 0 && !cancelPool.empty()) // random cancel trigger
    {
        std::uniform_int_distribution<size_t> idxDist(0, cancelPool.size() - 1);

        size_t idx = idxDist(rng); // pick random live order
        uint16_t cancelID = cancelPool[idx]; // grab ID to cancel

        cancelPool[idx] = cancelPool.back(); // swap-delete
        cancelPool.pop_back(); // remove last

        return {0, 0, cancelID, 2}; // cancel order (type 2)
    }

    // generate new order (no engine ID yet)
    trend = std::clamp(trend + trendStep(rng), -MAX_TREND, MAX_TREND); // drift update

    int bidOff = std::clamp((int)bidOffDist(rng), 1, 20); // bid offset noise
    int askOff = std::clamp((int)askOffDist(rng), 1, 20); // ask offset noise

    int op = opDist(rng); // pick order type

    uint32_t price = 0;
    uint8_t type = 0;
    uint16_t qty = (uint16_t)std::clamp((int)qtyDist(rng), 5, 500); // default qty

    switch (op)
    {
        case 0: price = (uint32_t)std::clamp((int)CENTER - bidOff + trend, 74000, 76047); type = 0; break; // passive bid
        case 1: price = (uint32_t)std::clamp((int)CENTER + askOff + trend, 74000, 76047); type = 1; break; // passive ask

        case 2: price = (uint32_t)std::clamp((int)CENTER + askOff + (int)aggOffDist(rng) + trend, 74000, 76047); type = 0; break; // aggressive buy
        case 3: price = (uint32_t)std::clamp((int)CENTER - bidOff - (int)aggOffDist(rng) + trend, 74000, 76047); type = 1; break; // aggressive sell

        case 4: type = 3; qty = (uint16_t)std::clamp((int)mktQtyDist(rng), 5, 200); break; // market buy
        case 5: type = 4; qty = (uint16_t)std::clamp((int)mktQtyDist(rng), 5, 200); break; // market sell
    }

    return {price, qty, 0xFFFF, type}; // return raw client order
}

void Gateway::run()
{
    uint64_t NS_PER_ORDER = 1'000'000'000ULL / targetRate; // spacing between orders in ns

    FILE* logFile = nullptr;
    if (logging)
    {
        logFile = fopen("logs/eventslog_pretty.txt", "w"); // event log output
        setvbuf(logFile, nullptr, _IOFBF, 1024 * 1024); // big buffer for speed
    }

    std::mt19937 rng(std::random_device{}()); // ext id generator
    std::uniform_int_distribution<uint32_t> extIDDist(0, UINT32_MAX); // fake external IDs

    auto nsNow = []() -> uint64_t {
        struct timespec ts;
        clock_gettime(CLOCK_MONOTONIC, &ts);
        return ts.tv_sec * 1'000'000'000ULL + ts.tv_nsec; // current time in ns
    };

    uint64_t nextSend = nsNow(); // next send timestamp
    uint32_t ordersPlaced = 0; // how many orders sent

    while (ordersPlaced < totalOrders)
    {
        Event e;
        while (engine.outboundQueue.pop(e)) // drain engine output queue
        {
            if (logging)
            {
                const char* eventType;

                if (e.type == 2)
                    eventType = "    ACK     "; // order accepted
                else if (e.type == 0)
                    eventType = "   CANCEL   "; // cancel event
                else if (e.type == 1 && e.price == 0 && e.fullyFilled)
                    eventType = e.quantity == 0 ? " MKT FILLED " : "MKT PARTIAL "; // market fill state
                else if (e.fullyFilled)
                    eventType = "    FILL    "; // full fill
                else
                    eventType = "PARTIAL FILL"; // partial fill

                const char* sideStr = (e.side == 0) ? "BUY " : "SELL";

                if (e.type == 1 && e.price == 0)
                    fprintf(logFile, "[%s] | %s | EXT_ID: %-12u | ENG_ID: %-6u | REMAINING QTY: %u\n",
                            eventType, sideStr, extID[e.orderID], e.orderID, e.quantity);
                else
                    fprintf(logFile, "[%s] | %s | EXT_ID: %-12u | ENG_ID: %-6u | $%.2f | QTY: %u\n",
                            eventType, sideStr, extID[e.orderID], e.orderID, e.price / 100.0f, e.quantity);
            }

            if (e.fullyFilled)
                releaseID(e.orderID); // recycle engine ID
        }

        if (nsNow() < nextSend) continue; // throttle order rate
        nextSend += NS_PER_ORDER;

        ClientOrder order = generateRandomOrder(); // create new simulated order

        if (order.type == 2)
        {
            engine.inboundQueue.push(order); // cancel already has engine ID
            ordersPlaced++;
            continue;
        }

        uint16_t id = getID(); // assign engine ID
        if (id == 0xFFFF) continue; // no IDs available

        order.orderID = id; // attach engine ID

        if (logging)
            extID[id] = extIDDist(rng); // map to fake external ID

        registerOrder(id, order.type); // track live order

        engine.inboundQueue.push(order); // send to engine
        ordersPlaced++;

        if (ordersPlaced == 200000 && logging)
        {
            fclose(logFile); // stop early logging
            printf("events logged to logs/eventslog.txt\n");
            logging = false;
            logFile = nullptr;
        }
    }

    Event e;
    while (engine.outboundQueue.pop(e)) // drain leftover engine events
    {
        if (e.fullyFilled)
            releaseID(e.orderID); // cleanup IDs
    }

    if (logging)
    {
        fclose(logFile); // final flush
        printf("events logged to logs/eventslog.txt\n");
    }

    running.store(false, std::memory_order_relaxed); // signal shutdown
}