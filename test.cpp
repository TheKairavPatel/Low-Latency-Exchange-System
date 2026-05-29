#include "engine.hpp"
#include "Gateway.hpp"
#include <thread>

std::atomic<bool> running(true);
Engine  engine(74000, running);
Gateway gw(engine, running);

int main()
{

    std::thread engineThread(&Engine::run, &engine);
    std::thread gatewayThread(&Gateway::run, &gw);

    gatewayThread.join();
    engineThread.join();

    return 0;
}