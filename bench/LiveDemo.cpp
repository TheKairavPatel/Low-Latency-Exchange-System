#include "../src/engine/engine.hpp"
#include "../src/gateway/Gateway.hpp"
#include "../src/snapshot/SnapshotWriter.hpp"
#include <thread>
#include <cstdio>
#include <pthread.h>
#include <sched.h>

// FULLY FINISHED

// demo build: runs full pipeline (gateway -> engine -> snapshot writer) with logging enabled
// used to show correctness + realistic system behavior under ~200k orders

std::atomic<bool> running(true); // global stop flag shared across all threads

Engine engine(74000, running); // matching engine with base price 74000 ticks
Gateway gw(engine, running, true, 200'000, 1000); // gateway: 200k orders total, 1k orders/sec (slow realistic feed)
SnapshotWriter snapshot(engine, running); // background snapshot dumper (order book state)

void pinThread(std::thread& t, int core)
{
    cpu_set_t cpuset; // init "set" of cores available for given thread to use, currently garbage
    CPU_ZERO(&cpuset); // memset all bits to 0 
    CPU_SET(core, &cpuset); // flips given bit corresponding to given core number to 1
    pthread_setaffinity_np(t.native_handle(), sizeof(cpu_set_t), &cpuset); // send to OS - make thread t only avaible to run on given core(s) based off cpuset
}

void setFIFO(std::thread& t, int priority)
{
    sched_param sp{priority}; // create OS scheduling struct holding priority and of type sched_param
    pthread_setschedparam(t.native_handle(), SCHED_FIFO, &sp); // send to OS - set given thread priority to highest, ensuring no other thread takes over 
}

int main()
{
    // start all system components as separate threads

    std::thread engineThread(&Engine::runDemo, &engine); // engine main loop (process orders)
    std::thread gatewayThread(&Gateway::run, &gw);       // gateway generates + feeds orders
    std::thread snapshotThread(&SnapshotWriter::run, &snapshot); // snapshots orderbook periodically

    // pin threads to physical cores for stable latency + less OS scheduling noise
    pinThread(engineThread, 1);
    pinThread(gatewayThread, 2);
    pinThread(snapshotThread, 3);

    // prioritize high
    setFIFO(engineThread, 99);
    setFIFO(gatewayThread, 98);
    // snapshot thread runs normal priority (non-critical path)

    gatewayThread.join(); // wait until all orders are generated and sent
    engineThread.join();  // wait until engine finishes processing queue
    snapshotThread.join(); // exits once running == false

    printf("demo done\n"); // end of full system run
    return 0;
}