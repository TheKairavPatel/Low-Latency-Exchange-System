#include "../src/engine/engine.hpp"
#include "../src/gateway/Gateway.hpp"
#include <thread>
#include <cstdio>
#include <pthread.h>
#include <sched.h>

// This benchmark runs engine + gateway at full throttle to measure raw throughput + latency under extreme load
// Engine processes orders, gateway generates them at a fixed high rate (stress test setup)

std::atomic<bool> running(true); // global shutdown flag shared across engine + gateway threads
Engine  engine(74000, running); // engine initialized with base price (74000 ticks)
Gateway gw(engine, running, false, 150'000'000, 100'000'000); 
// gateway: 150M total orders, target 100M orders/sec (intentionally above realistic capacity to stress system)

// pins a thread to a specific CPU core (reduces scheduler noise, improves latency consistency)
void pinThread(std::thread& t, int core) // reference to thread object, cannot copy thread object as a argument, core is dedicated cpu core we want to pin to
{
    cpu_set_t cpuset; // init "set" of cores available for given thread to use, currently garbage
    CPU_ZERO(&cpuset); // memset all bits to 0 
    CPU_SET(core, &cpuset); // flips given bit corresponding to given core number to 1
    pthread_setaffinity_np(t.native_handle(), sizeof(cpu_set_t), &cpuset); // send to OS - make thread t only avaible to run on given core(s) based off cpuset
}

// forces realtime scheduling (FIFO) with priority (engine usually higher priority than gateway)
void setFIFO(std::thread& t, int priority)
{
    sched_param sp{priority}; // create OS scheduling struct holding priority and of type sched_param
    pthread_setschedparam(t.native_handle(), SCHED_FIFO, &sp); // send to OS - set given thread priority to highest, ensuring no other thread takes over 
}

int main()
{
    auto start = std::chrono::high_resolution_clock::now(); 
    // start timer for full end-to-end throughput measurement

    std::thread engineThread(&Engine::run, &engine); // engine thread (order matching loop)
    std::thread gatewayThread(&Gateway::run, &gw);   // gateway thread (order generation + logging)

    pinThread(engineThread, 1);  // bind engine to core 1 (isolates from OS noise)
    pinThread(gatewayThread, 2); // bind gateway to core 2

    setFIFO(engineThread, 99);   // highest priority goes to engine (critical path)
    setFIFO(gatewayThread, 98);  // 

    gatewayThread.join(); // wait for gateway to finish generating all orders
    auto end = std::chrono::high_resolution_clock::now(); // stop timing after workload generation ends

    engineThread.join(); // ensure engine finishes processing remaining queued orders

    double seconds = std::chrono::duration<double>(end - start).count(); // total runtime in seconds
    double throughput = 150'000'000.0 / seconds; // compute orders/sec throughput

    printf("\nGateway throughput: %.2f million orders/sec\n", throughput / 1e6); // print final benchmark result
    return 0;
}