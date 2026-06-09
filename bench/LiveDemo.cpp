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
    FILE* readBack  = fopen("logs/eventslog.txt", "r");
    FILE* prettyFile = fopen("logs/eventslog_pretty.txt", "w");

    char lineBuf[256];
    (void)fgets(lineBuf, sizeof(lineBuf), readBack);

    while (fgets(lineBuf, sizeof(lineBuf), readBack))
    {
        uint32_t orderID, extIDVal, quantity, type, side, fullyFilled;
        float price;
        sscanf(lineBuf, "%u,%u,%f,%u,%u,%u,%u", &orderID, &extIDVal, &price, &quantity, &type, &side, &fullyFilled);

        const char* eventType;
        if (type == 0)
            eventType = "   CANCEL   ";
        else if (type == 1 && price == 0.0f && fullyFilled)
            eventType = quantity == 0 ? " MKT FILLED " : "MKT PARTIAL ";
        else if (fullyFilled)
            eventType = "    FILL    ";
        else
            eventType = "PARTIAL FILL";

        const char* sideStr = (side == 0) ? "BUY " : "SELL";

        if (type == 1 && price == 0.0f)
            fprintf(prettyFile, "[%s] | %s | EXT_ID: %-12u | ENG_ID: %-6u | REMAINING QTY: %u\n",
                    eventType, sideStr, extIDVal, orderID, quantity);
        else
            fprintf(prettyFile, "[%s] | %s | EXT_ID: %-12u | ENG_ID: %-6u | $%.2f | QTY: %u\n",
                    eventType, sideStr, extIDVal, orderID, price, quantity);
    }

    fclose(readBack);
    fclose(prettyFile);
    printf("pretty log written to logs/eventslog_pretty.txt\n");
    return 0;
}