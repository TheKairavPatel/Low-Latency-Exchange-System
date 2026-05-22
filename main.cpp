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

static constexpr uint32_t BASE = 74200;
static constexpr int N = 2'000'000;

uint16_t freeIDs[65536];
uint16_t topID = 0;
inline void initIDs() {
    for (int i = 0; i < 65536; i++) freeIDs[i] = i;
    topID = 0;
}
inline uint16_t allocID() { return freeIDs[topID++]; }
inline void freeID(uint16_t id) { freeIDs[--topID] = id; }
volatile uint64_t sink = 0;

// pregen arrays — no rng in hot loop
struct OrderSpec { uint32_t price; uint32_t qty; };
OrderSpec* insertSpecs;
OrderSpec* cancelSpecs;
struct MatchSpec { uint32_t askPrice; uint32_t buyPrice; uint32_t qty; };
MatchSpec* matchSpecs;

static Engine engine(BASE);

int main() {
    printf("starting\n"); fflush(stdout);
    initIDs();
    printf("IDs init\n"); fflush(stdout);
    printf("engine init\n"); fflush(stdout);

    std::mt19937 rng(42);
    // realistic SPY passive order distribution — clustered near mid
    std::normal_distribution<double> bidOffsetDist(10.0, 5.0);  // ~10 ticks below mid
    std::normal_distribution<double> askOffsetDist(10.0, 5.0);  // ~10 ticks above mid
    std::uniform_int_distribution<uint32_t> qtyDist(1, 500);
    std::uniform_int_distribution<uint32_t> aggOffsetDist(1, 20); // how far aggressive crosses

    // pregen insert specs — passive buys below mid
    insertSpecs = new OrderSpec[N];
    for (int i = 0; i < N; i++) {
        int offset = std::max(1, (int)bidOffsetDist(rng));
        offset = std::min(offset, 500); // clamp to valid range
        insertSpecs[i] = {BASE + 1000 - (uint32_t)offset, qtyDist(rng)};
    }

    // pregen cancel specs — same distribution as insert
    cancelSpecs = new OrderSpec[N];
    for (int i = 0; i < N; i++) {
        int offset = std::max(1, (int)bidOffsetDist(rng));
        offset = std::min(offset, 500);
        cancelSpecs[i] = {BASE + 1000 - (uint32_t)offset, qtyDist(rng)};
    }

    // pregen match specs — ask near mid, aggressive buy crosses it
    matchSpecs = new MatchSpec[N];
    for (int i = 0; i < N; i++) {
        int askOffset = std::max(1, (int)askOffsetDist(rng));
        askOffset = std::min(askOffset, 500);
        uint32_t askPrice = BASE + 1000 + (uint32_t)askOffset;
        uint32_t aggOffset = aggOffsetDist(rng);
        matchSpecs[i] = {askPrice, askPrice + aggOffset, qtyDist(rng)};
    }

    printf("pregen done\n"); fflush(stdout);

    std::vector<uint64_t> insertSamples, cancelSamples, matchSamples;
    insertSamples.reserve(N);
    cancelSamples.reserve(N);
    matchSamples.reserve(N);
    printf("vectors reserved\n"); fflush(stdout);

    // ── WARMUP ──
    for (int i = 0; i < 200000; i++) {
        uint16_t id = allocID();
        engine.processOrder({BASE + 1000, 100, id, 0});
        engine.processOrder({0, 0, id, 2});
        freeID(id);
    }
    printf("warmup done\n"); fflush(stdout);

    // ── INSERT BENCH ──
    // passive buys — no asks in book so guaranteed no match
    for (int i = 0; i < N; i++) {
        uint16_t id = allocID();
        uint64_t s = rdtsc_start();
        engine.processOrder({insertSpecs[i].price, insertSpecs[i].qty, id, 0});
        uint64_t e = rdtsc_end();
        insertSamples.push_back(e - s);
        sink ^= id;
        // dont track liveIDs here — cancel bench has its own setup
        // just free after to avoid running out of IDs
        engine.processOrder({0, 0, id, 2});
        freeID(id);
    }
    printf("insert bench done\n"); fflush(stdout);

    // ── CANCEL BENCH ──
    // insert N orders first then cancel them
    std::vector<uint16_t> liveIDs;
    liveIDs.reserve(N);
    for (int i = 0; i < N; i++) {
        uint16_t id = allocID();
        engine.processOrder({cancelSpecs[i].price, cancelSpecs[i].qty, id, 0});
        liveIDs.push_back(id);
    }
    printf("cancel setup done\n"); fflush(stdout);
    for (int i = 0; i < N; i++) {
        uint16_t id = liveIDs[i];
        uint64_t s = rdtsc_start();
        engine.processOrder({0, 0, id, 2});
        uint64_t e = rdtsc_end();
        cancelSamples.push_back(e - s);
        freeID(id);
        sink ^= id;
    }
    printf("cancel bench done\n"); fflush(stdout);

    // ── MATCH BENCH ──
    // place passive ask then aggressive buy that always crosses
    for (int i = 0; i < N; i++) {
        uint16_t id1 = allocID();
        uint16_t id2 = allocID();
        // passive ask
        engine.processOrder({matchSpecs[i].askPrice, matchSpecs[i].qty, id1, 1});
        // aggressive buy — always crosses
        uint64_t s = rdtsc_start();
        engine.processOrder({matchSpecs[i].buyPrice, matchSpecs[i].qty, id2, 0});
        uint64_t e = rdtsc_end();
        matchSamples.push_back(e - s);
        // if ask not fully filled, cancel it
        if (!engine.getBestAsk() != 2048)
            engine.processOrder({0, 0, id1, 2});
        freeID(id1);
        freeID(id2);
        sink ^= i;
    }
    printf("match bench done\n"); fflush(stdout);

    // ── STATS ──
    auto printStats = [&](std::vector<uint64_t>& v, const char* name) {
        std::sort(v.begin(), v.end());
        uint64_t sum = 0;
        for (auto x : v) sum += x;
        auto pct = [&](double p) -> uint64_t {
            return v[(size_t)(p * (v.size() - 1))];
        };
        printf("\n%s (N=%zu):\n", name, v.size());
        printf("  mean : %.2f cycles\n", (double)sum / v.size());
        printf("  p50  : %llu cycles\n", pct(0.50));
        printf("  p90  : %llu cycles\n", pct(0.90));
        printf("  p99  : %llu cycles\n", pct(0.99));
        printf("  p999 : %llu cycles\n", pct(0.999));
        printf("  max  : %llu cycles\n", v.back());
    };

    printStats(insertSamples, "INSERT");
    printStats(cancelSamples, "CANCEL");
    printStats(matchSamples,  "MATCH");
    printf("\nsink: %llu\n", sink);

    delete[] insertSpecs;
    delete[] cancelSpecs;
    delete[] matchSpecs;
    return 0;
}