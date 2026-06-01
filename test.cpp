#include "engine.hpp"
#include "Gateway.hpp"
#include <thread>
#include <stdio.h>

std::atomic<bool> running(true);
Engine  engine(74000, running);
Gateway gw(engine, running);

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
    std::thread engineThread(&Engine::run, &engine);
    std::thread gatewayThread(&Gateway::run, &gw);

    pinThread(engineThread, 1);
    pinThread(gatewayThread, 2);
    setFIFO(engineThread, 99);
    setFIFO(gatewayThread, 98);

    gatewayThread.join();
    engineThread.join();

    FILE* readBack = fopen("eventslog.txt", "r");
    FILE* prettyFile = fopen("eventslog_pretty.txt", "w");

    char lineBuf[256];
    fgets(lineBuf, sizeof(lineBuf), readBack);

    while (fgets(lineBuf, sizeof(lineBuf), readBack))
    {
        uint32_t orderID, extIDVal, price, quantity, type, side, fullyFilled;
        sscanf(lineBuf, "%u,%u,%u,%u,%u,%u,%u", &orderID, &extIDVal, &price, &quantity, &type, &side, &fullyFilled);

        const char* eventType;
        if (type == 0)
            eventType = "   CANCEL   ";
        else if (type == 1 && price == 0 && fullyFilled)
            eventType = quantity == 0 ? " MKT FILLED " : "MKT PARTIAL ";
        else if (fullyFilled)
            eventType = "    FILL    ";
        else
            eventType = "PARTIAL FILL";

        const char* sideStr = (side == 0) ? "BUY " : "SELL";

        if (type == 1 && price == 0)
            fprintf(prettyFile, "[%s] | %s | EXT_ID: %-12u | ENG_ID: %-6u | REMAINING QTY: %u\n",
                    eventType, sideStr, extIDVal, orderID, quantity);
        else
            fprintf(prettyFile, "[%s] | %s | EXT_ID: %-12u | ENG_ID: %-6u | $%u.00 | QTY: %u\n",
                    eventType, sideStr, extIDVal, orderID, price, quantity);
    }

    fclose(readBack);
    fclose(prettyFile);
    printf("pretty log written to eventslog_pretty.txt\n");
    return 0;
}