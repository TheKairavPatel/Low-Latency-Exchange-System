#include "../src/engine/engine.hpp"
#include "../src/gateway/Gateway.hpp"
#include "../src/snapshot/SnapshotWriter.hpp"
#include <thread>
#include <cstdio>
#include <pthread.h>
#include <sched.h>

std::atomic<bool> running(true);
Engine engine(74000, running);
Gateway gw(engine, running, true, 50'000);
SnapshotWriter snapshot(engine, running);

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

    std::thread engineThread(&Engine::runDemo, &engine);
    std::thread gatewayThread(&Gateway::run, &gw);
    std::thread snapshotThread(&SnapshotWriter::run, &snapshot);

    pinThread(engineThread, 1);
    pinThread(gatewayThread, 2);
    pinThread(snapshotThread, 3);

    setFIFO(engineThread, 99);
    setFIFO(gatewayThread, 98);
    // snapshot thread runs at normal priority — no FIFO

    gatewayThread.join();
    engineThread.join();
    snapshotThread.join(); // will exit when running = false

    printf("demo done\n");
    return 0;
}