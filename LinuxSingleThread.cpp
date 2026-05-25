#include <cstdio>
#include <cstdint>
#include <x86intrin.h>
#include <vector>
#include <algorithm>
#include <random>
#include <pthread.h>
#include <sched.h>
#include <unistd.h>
#include "engine.hpp"

static inline uint64_t rdtsc_start() {
    unsigned int lo, hi;
    __asm__ volatile ("rdtsc" : "=a"(lo), "=d"(hi));
    return ((uint64_t)hi << 32) | lo;
}

static inline uint64_t rdtsc_end() {
    unsigned int lo, hi;
    __asm__ volatile ("rdtsc" : "=a"(lo), "=d"(hi));
    return ((uint64_t)hi << 32) | lo;
}

static constexpr uint32_t BASE = 74000;
static constexpr int N = 2'000'000;
static constexpr int BOOK_DEPTH = 500;

uint16_t freeIDs[65536];
uint16_t topID = 0;

inline void initIDs() {
    for (int i = 0; i < 65536; i++) freeIDs[i] = i;
    topID = 0;
}

inline uint16_t allocID() { return freeIDs[topID++]; }
inline void freeID(uint16_t id) { freeIDs[--topID] = id; }

static Engine engine(BASE);
volatile uint64_t sink = 0;

// ── PREGEN ──
enum OpType : uint8_t {
    PASSIVE_BUY,
    PASSIVE_SELL,
    AGGRESSIVE_BUY,
    AGGRESSIVE_SELL,
    CANCEL_BID,
    CANCEL_ASK,
    MARKET_BUY,
    MARKET_SELL
};

struct PregenOp {
    OpType type;
    uint32_t price;
    uint32_t qty;
};

void pin_to_core(int core) {
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(core, &cpuset);
    pthread_setaffinity_np(pthread_self(), sizeof(cpu_set_t), &cpuset);
}

int main()
{
    pin_to_core(1);
    printf("pinned to core 1 (P-core)\n");

    initIDs();

    std::mt19937 rng(42);

    std::normal_distribution<double> bidOffDist(15.0, 5.0);
    std::normal_distribution<double> askOffDist(15.0, 5.0);
    std::uniform_int_distribution<uint32_t> aggOffDist(1, 5);
    std::uniform_int_distribution<uint32_t> qtyDist(1, 300);
    std::uniform_int_distribution<uint32_t> mktQtyDist(50, 500);

    // 28 passive buy, 28 passive sell, 12 agg buy, 12 agg sell,
    // 8 cancel bid, 8 cancel ask, 2 market buy, 2 market sell
    std::discrete_distribution<int> opDist({28, 28, 12, 12, 8, 8, 2, 2});

    PregenOp* ops = new PregenOp[N];

    for (int i = 0; i < N; i++)
    {
        OpType type = (OpType)opDist(rng);
        uint32_t qty = qtyDist(rng);
        uint32_t price = 0;

        int bidOff = std::max(1, std::min((int)bidOffDist(rng), 500));
        int askOff = std::max(1, std::min((int)askOffDist(rng), 500));

        switch (type)
        {
            case PASSIVE_BUY:
                price = BASE + 200 - bidOff;
                break;
            case PASSIVE_SELL:
                price = BASE + 200 + askOff;
                break;
            case AGGRESSIVE_BUY:
                price = BASE + 200 + askOff + aggOffDist(rng);
                break;
            case AGGRESSIVE_SELL:
                price = BASE + 200 - bidOff - aggOffDist(rng);
                break;
            case CANCEL_BID:
            case CANCEL_ASK:
                price = 0;
                qty = 0;
                break;
            case MARKET_BUY:
            case MARKET_SELL:
                price = 0;
                qty = mktQtyDist(rng);
                break;
        }

        ops[i] = {type, price, qty};
    }

    printf("pregen done\n"); fflush(stdout);

    // ── WARMUP ──
    std::vector<uint16_t> warmBids, warmAsks;
    warmBids.reserve(BOOK_DEPTH);
    warmAsks.reserve(BOOK_DEPTH);

    for (int i = 0; i < BOOK_DEPTH; i++)
    {
        uint16_t bidID = allocID();
        uint16_t askID = allocID();

        int bidOff = std::max(1, std::min((int)bidOffDist(rng), 500));
        int askOff = std::max(1, std::min((int)askOffDist(rng), 500));

        engine.processOrder({BASE + 200 - (uint32_t)bidOff, 100, bidID, 0});
        engine.processOrder({BASE + 200 + (uint32_t)askOff, 100, askID, 1});

        warmBids.push_back(bidID);
        warmAsks.push_back(askID);
    }

    printf("book depth built\n"); fflush(stdout);

    // ── LIVE TRACKING ──
    std::vector<uint16_t> liveBids, liveAsks;
    liveBids.reserve(65536);
    liveAsks.reserve(65536);

    for (auto id : warmBids) liveBids.push_back(id);
    for (auto id : warmAsks) liveAsks.push_back(id);

    std::vector<uint64_t> samples;
    samples.reserve(N);

    std::uniform_int_distribution<size_t> cancelDist;

    int trend = 0;
    static constexpr int MAX_TREND = 3000;
    std::normal_distribution<double> trendStep(0.0, 0.1);

    // ── HOT LOOP ──
    for (int i = 0; i < N; i++)
    {
        PregenOp& op = ops[i];
        uint64_t s, e = 0;

        int step = (int)std::round(trendStep(rng));
        if (trend + step > MAX_TREND)
            trend = MAX_TREND;
        else if (trend + step < -MAX_TREND)
            trend = -MAX_TREND;
        else
            trend += step;

        uint32_t adjustedPrice = op.price;
        if (op.type != CANCEL_BID && op.type != CANCEL_ASK &&
            op.type != MARKET_BUY && op.type != MARKET_SELL) {
            adjustedPrice = (uint32_t)std::max((int)BASE, std::min((int)(BASE + 2047), (int)op.price + trend));
        }

        switch (op.type)
        {
            case PASSIVE_BUY:
            {
                uint16_t id = allocID();
                ClientOrder o = {adjustedPrice, op.qty, id, 0};
                s = rdtsc_start();
                engine.processOrder(o);
                e = rdtsc_end();
                samples.push_back(e - s);
                if (engine.getBestBid() != LEVELS)
                    liveBids.push_back(id);
                else
                    freeID(id);
                break;
            }
            case PASSIVE_SELL:
            {
                uint16_t id = allocID();
                ClientOrder o = {adjustedPrice, op.qty, id, 1};
                s = rdtsc_start();
                engine.processOrder(o);
                e = rdtsc_end();
                samples.push_back(e - s);
                if (engine.getBestAsk() != LEVELS)
                    liveAsks.push_back(id);
                else
                    freeID(id);
                break;
            }
            case AGGRESSIVE_BUY:
            {
                uint16_t id = allocID();
                ClientOrder o = {adjustedPrice, op.qty, id, 0};
                s = rdtsc_start();
                engine.processOrder(o);
                e = rdtsc_end();
                samples.push_back(e - s);
                freeID(id);
                break;
            }
            case AGGRESSIVE_SELL:
            {
                uint16_t id = allocID();
                ClientOrder o = {adjustedPrice, op.qty, id, 1};
                s = rdtsc_start();
                engine.processOrder(o);
                e = rdtsc_end();
                samples.push_back(e - s);
                freeID(id);
                break;
            }
            case CANCEL_BID:
            {
                if (liveBids.empty()) break;
                cancelDist.param(
                    std::uniform_int_distribution<size_t>::param_type(0, liveBids.size() - 1)
                );
                size_t idx = cancelDist(rng);
                uint16_t id = liveBids[idx];
                liveBids[idx] = liveBids.back();
                liveBids.pop_back();
                ClientOrder o = {0, 0, id, 2};
                s = rdtsc_start();
                engine.processOrder(o);
                e = rdtsc_end();
                samples.push_back(e - s);
                freeID(id);
                break;
            }
            case CANCEL_ASK:
            {
                if (liveAsks.empty()) break;
                cancelDist.param(
                    std::uniform_int_distribution<size_t>::param_type(0, liveAsks.size() - 1)
                );
                size_t idx = cancelDist(rng);
                uint16_t id = liveAsks[idx];
                liveAsks[idx] = liveAsks.back();
                liveAsks.pop_back();
                ClientOrder o = {0, 0, id, 2};
                s = rdtsc_start();
                engine.processOrder(o);
                e = rdtsc_end();
                samples.push_back(e - s);
                freeID(id);
                break;
            }
            case MARKET_BUY:
            {
                uint16_t id = allocID();
                ClientOrder o = {0, op.qty, id, 3};
                s = rdtsc_start();
                engine.processOrder(o);
                e = rdtsc_end();
                samples.push_back(e - s);
                freeID(id);
                break;
            }
            case MARKET_SELL:
            {
                uint16_t id = allocID();
                ClientOrder o = {0, op.qty, id, 4};
                s = rdtsc_start();
                engine.processOrder(o);
                e = rdtsc_end();
                samples.push_back(e - s);
                freeID(id);
                break;
            }
        }

        sink ^= e;
    }

    printf("benchmark done\n"); fflush(stdout);

    std::sort(samples.begin(), samples.end());

    auto pct = [&](std::vector<uint64_t>& v, double p) -> uint64_t {
        return v[(size_t)(p * (v.size() - 1))];
    };

    uint64_t raw_sum = 0;
    for (auto x : samples) raw_sum += x;
    printf("\nRAW DATA (N=%zu):\n", samples.size());
    printf("  mean : %.2f cycles\n", (double)raw_sum / samples.size());
    printf("  p50  : %lu cycles\n", pct(samples, 0.50));
    printf("  p90  : %lu cycles\n", pct(samples, 0.90));
    printf("  p99  : %lu cycles\n", pct(samples, 0.99));
    printf("  p99.9: %lu cycles\n", pct(samples, 0.999));
    printf("  max  : %lu cycles\n", samples.back());

    uint64_t cutoff = pct(samples, 0.999);
    size_t before = samples.size();
    samples.erase(std::remove_if(samples.begin(), samples.end(),
        [cutoff](uint64_t x) { return x > cutoff; }), samples.end());
    size_t removed = before - samples.size();

    uint64_t sum = 0;
    for (auto x : samples) sum += x;

    printf("\nFILTERED (N=%zu, removed=%zu, cutoff=p99.9=%lu cycles):\n", samples.size(), removed, cutoff);
    printf("  mean : %.2f cycles\n", (double)sum / samples.size());
    printf("  p50  : %lu cycles\n", pct(samples, 0.50));
    printf("  p90  : %lu cycles\n", pct(samples, 0.90));
    printf("  p99  : %lu cycles\n", pct(samples, 0.99));
    printf("  p99.9: %lu cycles\n", pct(samples, 0.999));
    printf("  max  : %lu cycles\n", samples.back());
    printf("\nsink: %lu\n", (unsigned long)sink);

    delete[] ops;
    return 0;
}