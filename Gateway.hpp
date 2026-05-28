#include "engine.hpp"

class Gateway
{
    OutboundQueue& outboundQueue;
    InboundQueue& inboundQueue;
    uint16_t freeIDs[65536];
    uint16_t topID;
    bool isEmpty() { return topID == 0; }
    public:
    Gateway(OutboundQueue& outQ, InboundQueue& inQ) : outboundQueue(outQ), inboundQueue(inQ) {}
    void run();
};