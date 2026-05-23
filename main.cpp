#include <cstdio>
#include <cstdint>
#include <x86intrin.h>
#include <vector>
#include <algorithm>
#include <random>
#include "engine.hpp"

static inline uint64_t rdtsc_start() {
    unsigned aux;
    _mm_lfence();
    return __rdtscp(&aux);
}

static inline uint64_t rdtsc_end() {
    unsigned aux;
    uint64_t t = __rdtscp(&aux);
    _mm_lfence();
    return t;
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
    CANCEL_ASK
};

struct PregenOp {
    OpType type;
    uint32_t price;
    uint32_t qty;
};

int main()
{
    initIDs();

    std::mt19937 rng(42);

    std::normal_distribution<double> bidOffDist(15.0, 5.0);
    std::normal_distribution<double> askOffDist(15.0, 5.0);
    std::uniform_int_distribution<uint32_t> aggOffDist(1, 5);
    std::uniform_int_distribution<uint32_t> qtyDist(1, 300);

    std::discrete_distribution<int> opDist({35, 35, 10, 10, 5, 5});

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

    // ── HOT LOOP ──
    for (int i = 0; i < N; i++)
    {
        PregenOp& op = ops[i];
        uint64_t s, e;

        switch (op.type)
        {
            case PASSIVE_BUY:
            {
                uint16_t id = allocID();
                ClientOrder o = {op.price, op.qty, id, 0};

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
                ClientOrder o = {op.price, op.qty, id, 1};

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
                ClientOrder o = {op.price, op.qty, id, 0};

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
                ClientOrder o = {op.price, op.qty, id, 1};

                s = rdtsc_start();
                engine.processOrder(o);
                e = rdtsc_end();

                samples.push_back(e - s);

                freeID(id);
                break;
            }

            // ───────── RANDOMIZED CANCELS ─────────

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
        }

        sink ^= e;
    }

    printf("benchmark done\n"); fflush(stdout);

    //samples.erase(std::remove_if(samples.begin(), samples.end(),
      //  [](uint64_t x) { return x > 10000; }), samples.end());

    std::sort(samples.begin(), samples.end());

    uint64_t sum = 0;
    for (auto x : samples) sum += x;

    auto pct = [&](double p) -> uint64_t {
        return samples[(size_t)(p * (samples.size() - 1))];
    };

    printf("\nMIXED ORDER FLOW (N=%zu):\n", samples.size());
    printf("  mean : %.2f cycles\n", (double)sum / samples.size());
    printf("  p50  : %llu cycles\n", pct(0.50));
    printf("  p90  : %llu cycles\n", pct(0.90));
    printf("  p99  : %llu cycles\n", pct(0.99));
    printf("  p999 : %llu cycles\n", pct(0.999));
    printf("  max  : %llu cycles\n", samples.back());
    printf("\nsink: %llu\n", sink);

    delete[] ops;
    return 0;
}