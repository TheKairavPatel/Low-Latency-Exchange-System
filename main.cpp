#include <cstdio>
#include <cstdint>
#include <x86intrin.h>
#include <vector>
#include <algorithm>
#include "engine.hpp"

static Engine engine(74200);

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

std::vector<uint64_t> insertSamples;
std::vector<uint64_t> cancelSamples;
std::vector<uint64_t> matchSamples;

int main() {
    printf("starting\n"); fflush(stdout);
    initIDs();
    printf("IDs init\n"); fflush(stdout);
    printf("engine init\n"); fflush(stdout);

    insertSamples.reserve(N);
    cancelSamples.reserve(N);
    matchSamples.reserve(N);
    printf("vectors reserved\n"); fflush(stdout);

    // ── WARMUP ──
    for (int i = 0; i < 1000; i++) {
        uint16_t id = allocID();
        engine.processOrder({BASE + 1, 100, id, 0});
        engine.processOrder({0, 0, id, 2});
        freeID(id);
    }
    printf("warmup done\n"); fflush(stdout);

    // ── INSERT BENCH ──
    for (int i = 0; i < N; i++) {
        uint16_t id = allocID();
        uint64_t s = rdtsc_start();
        engine.processOrder({BASE + 200, 100, id, 0});
        uint64_t e = rdtsc_end();
        insertSamples.push_back(e - s);
        sink ^= id;
    }
    printf("insert bench done\n"); fflush(stdout);

    // ── CANCEL BENCH ──
    std::vector<uint16_t> liveIDs;
    liveIDs.reserve(N);
    printf("liveIDs reserved\n"); fflush(stdout);
    for (int i = 0; i < N; i++) {
        uint16_t id = allocID();
        engine.processOrder({BASE + 200, 100, id, 0});
        liveIDs.push_back(id);
    }
    printf("cancel setup done\n"); fflush(stdout);
    for (int i = 0; i < N; i++) {
        uint16_t id = liveIDs[i];
        uint64_t s = rdtsc_start();
        engine.processOrder({0, 0, id, 2});
        uint64_t e = rdtsc_end();
        cancelSamples.push_back(e - s);
        sink ^= id;
    }
    printf("cancel bench done\n"); fflush(stdout);

    // ── MATCH BENCH ──
    for (int i = 0; i < N; i++) {
        uint16_t id1 = allocID();
        uint16_t id2 = allocID();
        engine.processOrder({BASE + 200, 100, id1, 1});
        uint64_t s = rdtsc_start();
        engine.processOrder({BASE + 300, 100, id2, 0});
        uint64_t e = rdtsc_end();
        matchSamples.push_back(e - s);
        freeID(id1);
        freeID(id2);
    }
    printf("match bench done\n"); fflush(stdout);

    // ── STATS ──
    auto printStats = [&](std::vector<uint64_t>& v, const char* name) {
        std::sort(v.begin(), v.end());
        uint64_t sum = 0;
        for (auto x : v) sum += x;
        auto pct = [&](double p) {
            return v[(size_t)(p * v.size())];
        };
        printf("\n%s:\n", name);
        printf("mean: %.2f cycles\n", (double)sum / v.size());
        printf("p50 : %llu\n", pct(0.50));
        printf("p90 : %llu\n", pct(0.90));
        printf("p99 : %llu\n", pct(0.99));
        printf("p999: %llu\n", pct(0.999));
    };

    printStats(insertSamples, "INSERT");
    printStats(cancelSamples, "CANCEL");
    printStats(matchSamples,  "MATCH");
    printf("\nsink: %llu\n", sink);
    return 0;
}