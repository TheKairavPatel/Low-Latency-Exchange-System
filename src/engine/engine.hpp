#pragma once
#include <atomic>
#include "priceLevel.hpp"
#include "../gateway/Queues.hpp"
#include <chrono>

// FULLY FINISHED

static constexpr uint32_t LEVELS = 2048; // 2048 ($20.48 range) 

class Engine
{
    GlobalOrderInfo globalOrderInfos[65536]; // global lookup table indexed by orderID for O(1) cancel
    std::atomic<bool>& running; // reference to the running flag from main, used to signal the engine to stop when the program is exiting
    uint64_t buyBitmap[32]; // bitmap to track which buy price levels are occupied, each bit represents whether the corresponding price level in buyLevels has any orders, used for O(1) best bid/ask retrieval
    uint64_t sellBitmap[32]; // same as buyBitmap but for sell price levels

    public:
    uint32_t basePrice; // base price (lowest price allowed in the order book) in ticks
    // array of price levels for buy orders, each price level for every cent increment from and including base price
    PriceLevel buyLevels[LEVELS]; 
    // same as buyLevels but for sell orders, each price level for every cent increment from and including base price
    PriceLevel sellLevels[LEVELS];
    OutboundQueue outboundQueue; // SPSC queue for sending events from engine to gateway
    InboundQueue inboundQueue; // SPSC queue for receiving orders from gateway
    SnapshotQueue snapshotQueue; // SPSC queue for pushing book snapshots
    uint64_t totalOrders; // simple metric to track total number of orders processed by the engine
    std::chrono::steady_clock::time_point lastSnapshot;

    Engine(uint32_t basePrice, std::atomic<bool>& running); // constructor to initialize the engine with a base price and a reference to the running flag
    void processOrder(const ClientOrder& order); // proccess an incoming order from inbound queue, handle it and push events to outbound queue
    uint16_t getBestBid(); // get the best bid price by finding the highest set bit in the buyBitmap, corresponding to the index of price level in book
    uint16_t getBestAsk(); // get the best ask price by finding the lowest set bit in the sellBitmap, corresponding to the index of price level in book
    void buildSnapshot(); // build simple snapshot near spread
    void run(); // main loop for benchmarking, continuously process incoming orders from inbound until running is false while measuring processing latency
    void runDemo(); // main loop for demo, same as normal run but no benchmarking
};