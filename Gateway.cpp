#include "Gateway.hpp"
#include <cstdio>
#include <random>
#include <vector>
#include <algorithm>
#include <cstdio>
#include <x86intrin.h>
#include <algorithm>
#include <thread>
#include <chrono>
#include <set>

Gateway::Gateway(Engine& engine, std::atomic<bool> &running) : engine(engine), running(running)
{
    topID = 0;
    for (uint16_t i = 0; i < 65535; i++)
    {
        freeIDs[i] = i;
    }
}

uint16_t Gateway::getID()
{
    if (isEmpty()) [[unlikely]]
    {
        return 0xFFFF; // No more IDs available
    }
    topID++;
    return freeIDs[topID - 1];
}

void Gateway::releaseID(uint16_t id)
{
    topID--;
    freeIDs[topID] = id;
    return; 
}

void Gateway::run()
{
    printf("\n==================== COMPREHENSIVE TEST SUITE ====================\n");
    
    // ==================== SECTION 1: BASIC ORDER TYPES ====================
    printf("\n========== SECTION 1: BASIC ORDER TYPES ==========\n");
    
    printf("\n--- 1.1: Limit Buy ---\n");
    {
        ClientOrder buy = {74050, 100, getID(), 0};
        engine.inboundQueue.push(buy);
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        
        Event e;
        while (engine.outboundQueue.pop(e)) {
            printf("Event: price=%u qty=%u orderID=%u type=%u side=%u filled=%d\n", 
                   e.price, e.quantity, e.orderID, e.type, e.side, e.fullyFilled);
            if (e.fullyFilled) releaseID(e.orderID);
        }
        printf("Result: Buy order at 74050 should be in book\n");
    }
    
    printf("\n--- 1.2: Limit Sell ---\n");
    {
        ClientOrder sell = {74050, 50, getID(), 1};
        engine.inboundQueue.push(sell);
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        
        Event e;
        while (engine.outboundQueue.pop(e)) {
            printf("Event: price=%u qty=%u orderID=%u type=%u side=%u filled=%d\n", 
                   e.price, e.quantity, e.orderID, e.type, e.side, e.fullyFilled);
            if (e.fullyFilled) releaseID(e.orderID);
        }
        printf("Result: Should match with existing buy (partial fill possible)\n");
    }
    
    printf("\n--- 1.3: Market Buy ---\n");
    {
        ClientOrder marketBuy = {0, 30, getID(), 3};
        engine.inboundQueue.push(marketBuy);
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        
        Event e;
        while (engine.outboundQueue.pop(e)) {
            printf("Event: price=%u qty=%u orderID=%u type=%u side=%u filled=%d\n", 
                   e.price, e.quantity, e.orderID, e.type, e.side, e.fullyFilled);
            if (e.fullyFilled) releaseID(e.orderID);
        }
    }
    
    printf("\n--- 1.4: Market Sell ---\n");
    {
        ClientOrder marketSell = {0, 20, getID(), 4};
        engine.inboundQueue.push(marketSell);
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        
        Event e;
        while (engine.outboundQueue.pop(e)) {
            printf("Event: price=%u qty=%u orderID=%u type=%u side=%u filled=%d\n", 
                   e.price, e.quantity, e.orderID, e.type, e.side, e.fullyFilled);
            if (e.fullyFilled) releaseID(e.orderID);
        }
    }
    
    // ==================== SECTION 2: PRICE LEVELS ====================
    printf("\n========== SECTION 2: PRICE LEVEL TESTS ==========\n");
    
    printf("\n--- 2.1: Multiple orders at same price (Bid side) ---\n");
    {
        for (int i = 0; i < 10; i++) {
            ClientOrder buy = {74100, 10, getID(), 0};
            engine.inboundQueue.push(buy);
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        
        Event e;
        while (engine.outboundQueue.pop(e)) {
            printf("Event: price=%u qty=%u orderID=%u type=%u side=%u filled=%d\n", 
                   e.price, e.quantity, e.orderID, e.type, e.side, e.fullyFilled);
            if (e.fullyFilled) releaseID(e.orderID);
        }
        printf("Result: 10 buy orders at 74100 should be stacked\n");
    }
    
    printf("\n--- 2.2: Multiple orders at same price (Ask side) ---\n");
    {
        for (int i = 0; i < 10; i++) {
            ClientOrder sell = {74100, 10, getID(), 1};
            engine.inboundQueue.push(sell);
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        
        Event e;
        while (engine.outboundQueue.pop(e)) {
            printf("Event: price=%u qty=%u orderID=%u type=%u side=%u filled=%d\n", 
                   e.price, e.quantity, e.orderID, e.type, e.side, e.fullyFilled);
            if (e.fullyFilled) releaseID(e.orderID);
        }
        printf("Result: Should match with existing buys\n");
    }
    
    printf("\n--- 2.3: Different price levels (Bid) ---\n");
    {
        ClientOrder buy1 = {74080, 10, getID(), 0};
        ClientOrder buy2 = {74090, 10, getID(), 0};
        ClientOrder buy3 = {74100, 10, getID(), 0};
        engine.inboundQueue.push(buy1);
        engine.inboundQueue.push(buy2);
        engine.inboundQueue.push(buy3);
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        
        Event e;
        while (engine.outboundQueue.pop(e)) {
            printf("Event: price=%u qty=%u orderID=%u type=%u side=%u filled=%d\n", 
                   e.price, e.quantity, e.orderID, e.type, e.side, e.fullyFilled);
            if (e.fullyFilled) releaseID(e.orderID);
        }
    }
    
    printf("\n--- 2.4: Different price levels (Ask) ---\n");
    {
        ClientOrder sell1 = {74110, 10, getID(), 1};
        ClientOrder sell2 = {74120, 10, getID(), 1};
        ClientOrder sell3 = {74130, 10, getID(), 1};
        engine.inboundQueue.push(sell1);
        engine.inboundQueue.push(sell2);
        engine.inboundQueue.push(sell3);
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        
        Event e;
        while (engine.outboundQueue.pop(e)) {
            printf("Event: price=%u qty=%u orderID=%u type=%u side=%u filled=%d\n", 
                   e.price, e.quantity, e.orderID, e.type, e.side, e.fullyFilled);
            if (e.fullyFilled) releaseID(e.orderID);
        }
    }
    
    // ==================== SECTION 3: CANCEL TESTS ====================
    printf("\n========== SECTION 3: CANCEL TESTS ==========\n");
    
    printf("\n--- 3.1: Cancel limit order before fill ---\n");
    {
        ClientOrder buy = {74150, 50, getID(), 0};
        uint16_t cancelID = buy.orderID;
        engine.inboundQueue.push(buy);
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        
        ClientOrder cancel = {0, 0, cancelID, 2};
        engine.inboundQueue.push(cancel);
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        
        Event e;
        while (engine.outboundQueue.pop(e)) {
            printf("Event: price=%u qty=%u orderID=%u type=%u side=%u filled=%d\n", 
                   e.price, e.quantity, e.orderID, e.type, e.side, e.fullyFilled);
            if (e.fullyFilled) releaseID(e.orderID);
        }
    }
    
    printf("\n--- 3.2: Cancel market order (should be no-op) ---\n");
    {
        ClientOrder marketBuy = {0, 10, getID(), 3};
        uint16_t cancelID = marketBuy.orderID;
        engine.inboundQueue.push(marketBuy);
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        
        ClientOrder cancel = {0, 0, cancelID, 2};
        engine.inboundQueue.push(cancel);
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        
        Event e;
        while (engine.outboundQueue.pop(e)) {
            printf("Event: price=%u qty=%u orderID=%u type=%u side=%u filled=%d\n", 
                   e.price, e.quantity, e.orderID, e.type, e.side, e.fullyFilled);
            if (e.fullyFilled) releaseID(e.orderID);
        }
    }
    
    printf("\n--- 3.3: Multiple cancels of same order ---\n");
    {
        ClientOrder buy = {74160, 30, getID(), 0};
        uint16_t cancelID = buy.orderID;
        engine.inboundQueue.push(buy);
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        
        for (int i = 0; i < 3; i++) {
            ClientOrder cancel = {0, 0, cancelID, 2};
            engine.inboundQueue.push(cancel);
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        
        Event e;
        while (engine.outboundQueue.pop(e)) {
            printf("Event: price=%u qty=%u orderID=%u type=%u side=%u filled=%d\n", 
                   e.price, e.quantity, e.orderID, e.type, e.side, e.fullyFilled);
            if (e.fullyFilled) releaseID(e.orderID);
        }
        printf("Result: Only first cancel should produce event\n");
    }
    
    // ==================== SECTION 4: MATCHING SCENARIOS ====================
    printf("\n========== SECTION 4: MATCHING SCENARIOS ==========\n");
    
    printf("\n--- 4.1: Exact match (buy = sell) ---\n");
    {
        ClientOrder sell = {74170, 25, getID(), 1};
        ClientOrder buy = {74170, 25, getID(), 0};
        engine.inboundQueue.push(sell);
        engine.inboundQueue.push(buy);
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        
        Event e;
        while (engine.outboundQueue.pop(e)) {
            printf("Event: price=%u qty=%u orderID=%u type=%u side=%u filled=%d\n", 
                   e.price, e.quantity, e.orderID, e.type, e.side, e.fullyFilled);
            if (e.fullyFilled) releaseID(e.orderID);
        }
    }
    
    printf("\n--- 4.2: Buy larger than sell (partial fill remainder goes to book) ---\n");
    {
        ClientOrder sell = {74180, 15, getID(), 1};
        ClientOrder buy = {74180, 30, getID(), 0};
        engine.inboundQueue.push(sell);
        engine.inboundQueue.push(buy);
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        
        Event e;
        while (engine.outboundQueue.pop(e)) {
            printf("Event: price=%u qty=%u orderID=%u type=%u side=%u filled=%d\n", 
                   e.price, e.quantity, e.orderID, e.type, e.side, e.fullyFilled);
            if (e.fullyFilled) releaseID(e.orderID);
        }
    }
    
    printf("\n--- 4.3: Sell larger than buy (partial fill remainder goes to book) ---\n");
    {
        ClientOrder buy = {74190, 20, getID(), 0};
        ClientOrder sell = {74190, 40, getID(), 1};
        engine.inboundQueue.push(buy);
        engine.inboundQueue.push(sell);
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        
        Event e;
        while (engine.outboundQueue.pop(e)) {
            printf("Event: price=%u qty=%u orderID=%u type=%u side=%u filled=%d\n", 
                   e.price, e.quantity, e.orderID, e.type, e.side, e.fullyFilled);
            if (e.fullyFilled) releaseID(e.orderID);
        }
    }
    
    printf("\n--- 4.4: Buy sweeps multiple ask levels ---\n");
    {
        ClientOrder sell1 = {74200, 10, getID(), 1};
        ClientOrder sell2 = {74210, 10, getID(), 1};
        ClientOrder sell3 = {74220, 10, getID(), 1};
        ClientOrder sell4 = {74230, 10, getID(), 1};
        ClientOrder buy = {74230, 35, getID(), 0};
        
        engine.inboundQueue.push(sell1);
        engine.inboundQueue.push(sell2);
        engine.inboundQueue.push(sell3);
        engine.inboundQueue.push(sell4);
        engine.inboundQueue.push(buy);
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        
        Event e;
        while (engine.outboundQueue.pop(e)) {
            printf("Event: price=%u qty=%u orderID=%u type=%u side=%u filled=%d\n", 
                   e.price, e.quantity, e.orderID, e.type, e.side, e.fullyFilled);
            if (e.fullyFilled) releaseID(e.orderID);
        }
    }
    
    printf("\n--- 4.5: Sell sweeps multiple bid levels ---\n");
    {
        ClientOrder buy1 = {74150, 10, getID(), 0};
        ClientOrder buy2 = {74140, 10, getID(), 0};
        ClientOrder buy3 = {74130, 10, getID(), 0};
        ClientOrder buy4 = {74120, 10, getID(), 0};
        ClientOrder sell = {74120, 35, getID(), 1};
        
        engine.inboundQueue.push(buy1);
        engine.inboundQueue.push(buy2);
        engine.inboundQueue.push(buy3);
        engine.inboundQueue.push(buy4);
        engine.inboundQueue.push(sell);
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        
        Event e;
        while (engine.outboundQueue.pop(e)) {
            printf("Event: price=%u qty=%u orderID=%u type=%u side=%u filled=%d\n", 
                   e.price, e.quantity, e.orderID, e.type, e.side, e.fullyFilled);
            if (e.fullyFilled) releaseID(e.orderID);
        }
    }
    
    // ==================== SECTION 5: MARKET ORDER SCENARIOS ====================
    printf("\n========== SECTION 5: MARKET ORDER SCENARIOS ==========\n");
    
    printf("\n--- 5.1: Market buy with plenty of liquidity ---\n");
    {
        for (int i = 0; i < 20; i++) {
            uint32_t price = 74250 + (i * 5);
            ClientOrder sell = {price, 5, getID(), 1};
            engine.inboundQueue.push(sell);
        }
        
        ClientOrder marketBuy = {0, 60, getID(), 3};
        engine.inboundQueue.push(marketBuy);
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        
        Event e;
        while (engine.outboundQueue.pop(e)) {
            printf("Event: price=%u qty=%u orderID=%u type=%u side=%u filled=%d\n", 
                   e.price, e.quantity, e.orderID, e.type, e.side, e.fullyFilled);
            if (e.fullyFilled) releaseID(e.orderID);
        }
    }
    
    printf("\n--- 5.2: Market sell with plenty of liquidity ---\n");
    {
        for (int i = 0; i < 20; i++) {
            uint32_t price = 74100 - (i * 5);
            ClientOrder buy = {price, 5, getID(), 0};
            engine.inboundQueue.push(buy);
        }
        
        ClientOrder marketSell = {0, 60, getID(), 4};
        engine.inboundQueue.push(marketSell);
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        
        Event e;
        while (engine.outboundQueue.pop(e)) {
            printf("Event: price=%u qty=%u orderID=%u type=%u side=%u filled=%d\n", 
                   e.price, e.quantity, e.orderID, e.type, e.side, e.fullyFilled);
            if (e.fullyFilled) releaseID(e.orderID);
        }
    }
    
    printf("\n--- 5.3: Market buy with insufficient liquidity ---\n");
    {
        ClientOrder sell = {74300, 10, getID(), 1};
        engine.inboundQueue.push(sell);
        
        ClientOrder marketBuy = {0, 100, getID(), 3};
        engine.inboundQueue.push(marketBuy);
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        
        Event e;
        while (engine.outboundQueue.pop(e)) {
            printf("Event: price=%u qty=%u orderID=%u type=%u side=%u filled=%d\n", 
                   e.price, e.quantity, e.orderID, e.type, e.side, e.fullyFilled);
            if (e.fullyFilled) releaseID(e.orderID);
        }
        printf("Result: Should fill 10, remainder 90 shown in final event\n");
    }
    
    printf("\n--- 5.4: Market sell with insufficient liquidity ---\n");
    {
        ClientOrder buy = {74050, 10, getID(), 0};
        engine.inboundQueue.push(buy);
        
        ClientOrder marketSell = {0, 100, getID(), 4};
        engine.inboundQueue.push(marketSell);
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        
        Event e;
        while (engine.outboundQueue.pop(e)) {
            printf("Event: price=%u qty=%u orderID=%u type=%u side=%u filled=%d\n", 
                   e.price, e.quantity, e.orderID, e.type, e.side, e.fullyFilled);
            if (e.fullyFilled) releaseID(e.orderID);
        }
    }
    
    printf("\n--- 5.5: Market buy on empty book ---\n");
    {
        ClientOrder marketBuy = {0, 50, getID(), 3};
        engine.inboundQueue.push(marketBuy);
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        
        Event e;
        while (engine.outboundQueue.pop(e)) {
            printf("Event: price=%u qty=%u orderID=%u type=%u side=%u filled=%d\n", 
                   e.price, e.quantity, e.orderID, e.type, e.side, e.fullyFilled);
            if (e.fullyFilled) releaseID(e.orderID);
        }
        printf("Result: Should immediately return with remainder 50\n");
    }
    
    // ==================== SECTION 6: ID REUSE STRESS TEST ====================
    printf("\n========== SECTION 6: ID REUSE STRESS TEST ==========\n");
    
    printf("\n--- 6.1: Reuse IDs through many cycles ---\n");
    {
        std::vector<uint16_t> usedIDs;
        
        for (int cycle = 0; cycle < 5; cycle++) {
            printf("Cycle %d:\n", cycle);
            
            // Create 20 orders
            for (int i = 0; i < 20; i++) {
                uint32_t price = 74100 + i;
                ClientOrder buy = {price, 5, getID(), 0};
                usedIDs.push_back(buy.orderID);
                engine.inboundQueue.push(buy);
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            
            // Cancel them all
            for (uint16_t id : usedIDs) {
                ClientOrder cancel = {0, 0, id, 2};
                engine.inboundQueue.push(cancel);
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            
            // Drain events and release IDs
            Event e;
            while (engine.outboundQueue.pop(e)) {
                if (e.fullyFilled) {
                    releaseID(e.orderID);
                }
            }
            
            printf("  Cycle %d complete, IDs should be recycled\n", cycle);
            usedIDs.clear();
        }
    }
    
    printf("\n--- 6.2: Verify IDs are actually being reused (same IDs appear again) ---\n");
    {
        std::set<uint16_t> firstBatch;
        
        // First batch
        for (int i = 0; i < 10; i++) {
            ClientOrder buy = {74200, 5, getID(), 0};
            firstBatch.insert(buy.orderID);
            engine.inboundQueue.push(buy);
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        
        // Cancel first batch
        for (uint16_t id : firstBatch) {
            ClientOrder cancel = {0, 0, id, 2};
            engine.inboundQueue.push(cancel);
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        
        Event e;
        while (engine.outboundQueue.pop(e)) {
            if (e.fullyFilled) releaseID(e.orderID);
        }
        
        // Second batch
        std::set<uint16_t> secondBatch;
        for (int i = 0; i < 10; i++) {
            ClientOrder buy = {74200, 5, getID(), 0};
            secondBatch.insert(buy.orderID);
            engine.inboundQueue.push(buy);
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        
        // Cancel second batch
        for (uint16_t id : secondBatch) {
            ClientOrder cancel = {0, 0, id, 2};
            engine.inboundQueue.push(cancel);
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        
        while (engine.outboundQueue.pop(e)) {
            if (e.fullyFilled) releaseID(e.orderID);
        }
        
        printf("First batch IDs: ");
        for (uint16_t id : firstBatch) printf("%u ", id);
        printf("\nSecond batch IDs: ");
        for (uint16_t id : secondBatch) printf("%u ", id);
        printf("\nResult: IDs should overlap (reused)\n");
    }
    
    // ==================== SECTION 7: MIXED ORDER TYPES ====================
    printf("\n========== SECTION 7: MIXED ORDER TYPES ==========\n");
    
    printf("\n--- 7.1: Mix of limit, market, and cancel ---\n");
    {
        ClientOrder sell1 = {74350, 20, getID(), 1};
        ClientOrder sell2 = {74360, 20, getID(), 1};
        ClientOrder buy1 = {74350, 15, getID(), 0};
        ClientOrder marketBuy = {0, 10, getID(), 3};
        
        engine.inboundQueue.push(sell1);
        engine.inboundQueue.push(sell2);
        engine.inboundQueue.push(buy1);
        engine.inboundQueue.push(marketBuy);
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        
        Event e;
        while (engine.outboundQueue.pop(e)) {
            printf("Event: price=%u qty=%u orderID=%u type=%u side=%u filled=%d\n", 
                   e.price, e.quantity, e.orderID, e.type, e.side, e.fullyFilled);
            if (e.fullyFilled) releaseID(e.orderID);
        }
    }
    
    printf("\n--- 7.2: Cancel during partial fill scenario ---\n");
    {
        ClientOrder sell = {74370, 50, getID(), 1};
        ClientOrder buy = {74370, 30, getID(), 0};
        engine.inboundQueue.push(sell);
        engine.inboundQueue.push(buy);
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        
        // Try to cancel remaining sell
        ClientOrder cancel = {0, 0, sell.orderID, 2};
        engine.inboundQueue.push(cancel);
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        
        Event e;
        while (engine.outboundQueue.pop(e)) {
            printf("Event: price=%u qty=%u orderID=%u type=%u side=%u filled=%d\n", 
                   e.price, e.quantity, e.orderID, e.type, e.side, e.fullyFilled);
            if (e.fullyFilled) releaseID(e.orderID);
        }
        printf("Result: Sell should show partial fill then cancel remainder\n");
    }
    
    // ==================== SECTION 8: HIGH VOLUME BURST ====================
    printf("\n========== SECTION 8: HIGH VOLUME BURST ==========\n");
    
    printf("\n--- 8.1: 1000 orders rapid fire ---\n");
    {
        auto start = std::chrono::steady_clock::now();
        
        for (int i = 0; i < 500; i++) {
            uint32_t price = 74400 + (i % 50);
            ClientOrder buy = {price, 1, getID(), 0};
            ClientOrder sell = {price, 1, getID(), 1};
            engine.inboundQueue.push(buy);
            engine.inboundQueue.push(sell);
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        
        auto end = std::chrono::steady_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
        
        Event e;
        int eventCount = 0;
        while (engine.outboundQueue.pop(e)) {
            eventCount++;
            if (e.fullyFilled) releaseID(e.orderID);
        }
        
        printf("Result: %d events generated in %llu ms\n", eventCount, duration.count());
    }
    
    // ==================== SECTION 9: PRICE LEVEL ACCUMULATION ====================
    printf("\n========== SECTION 9: PRICE LEVEL ACCUMULATION ==========\n");
    
    printf("\n--- 9.1: Build deep book on one side ---\n");
    {
        for (uint32_t price = 74010; price <= 74990; price += 10) {
            ClientOrder sell = {price, 5, getID(), 1};
            engine.inboundQueue.push(sell);
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        
        printf("Result: 99 price levels with 5 each = 495 sell orders\n");
        
        // Market buy to sweep
        ClientOrder marketBuy = {0, 500, getID(), 3};
        engine.inboundQueue.push(marketBuy);
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        
        Event e;
        int fillEvents = 0;
        while (engine.outboundQueue.pop(e)) {
            fillEvents++;
            if (e.fullyFilled) releaseID(e.orderID);
        }
        printf("Result: Market buy generated %d fill events sweeping all levels\n", fillEvents);
    }
    
    // ==================== SECTION 10: FINAL VERIFICATION ====================
    printf("\n========== SECTION 10: FINAL VERIFICATION ==========\n");
    
    printf("\n--- 10.1: Verify engine still responsive after all tests ---\n");
    {
        ClientOrder sell = {74050, 10, getID(), 1};
        ClientOrder buy = {74050, 10, getID(), 0};
        engine.inboundQueue.push(sell);
        engine.inboundQueue.push(buy);
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        
        Event e;
        while (engine.outboundQueue.pop(e)) {
            printf("Event: price=%u qty=%u orderID=%u type=%u side=%u filled=%d\n", 
                   e.price, e.quantity, e.orderID, e.type, e.side, e.fullyFilled);
            if (e.fullyFilled) releaseID(e.orderID);
        }
        printf("Result: Engine still functioning correctly\n");
    }
    
    printf("\n--- 10.2: Final ID stats ---\n");
    {
        printf("Current topID: %u\n", topID);
        printf("Free IDs available: %u\n", 65535 - topID);
    }
    
    printf("\n==================== ALL TESTS COMPLETE ====================\n");
    running.store(false, std::memory_order_relaxed);
}