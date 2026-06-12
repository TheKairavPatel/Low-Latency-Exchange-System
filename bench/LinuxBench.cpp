#include "../src/engine/engine.hpp"
#include "../src/gateway/Gateway.hpp"
#include <thread>
#include <cstdio>
#include <pthread.h>
#include <sched.h>

// This benchmark runs the engine and gateway at full speed (logging stops after 200k), simulating a high-load scenario to test raw throughput and latency under stress
// Total orders set to 150 million, orders per second set to 100 million (past gateway capacity) to stress test

std::atomic<bool> running(true);
Engine  engine(74000, running);
Gateway gw(engine, running, false, 150'000'000, 100'000'000);

void pinThread(std::thread& t, int core)
{
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(core, &cpuset);
    pthread_setaffinity_np(t.native_handle(), sizeof(cpu_set_t), &cpuset);
}

void setFIFO(std::thread& t, int priority)
{
    sched_param sp{priority};
    pthread_setschedparam(t.native_handle(), SCHED_FIFO, &sp);
}

int main()
{
    auto start = std::chrono::high_resolution_clock::now();
    std::thread engineThread(&Engine::run, &engine);
    std::thread gatewayThread(&Gateway::run, &gw);

    pinThread(engineThread, 1);
    pinThread(gatewayThread, 2);
    setFIFO(engineThread, 99);
    setFIFO(gatewayThread, 98);

    gatewayThread.join();
    auto end = std::chrono::high_resolution_clock::now();
    engineThread.join();
    double seconds = std::chrono::duration<double>(end - start).count();
    double throughput = 150'000'000.0 / seconds;
    printf("\nGateway throughput: %.2f million orders/sec\n", throughput / 1e6);
    return 0;
}