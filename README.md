# Low-Latency C++ Exchange System

A simulated exchange system built to explore the systems techniques used in HFT/Exchanges. Implements a full order-matching pipeline — gateway, matching engine, and snapshot writer — running across isolated CPU cores with real-time scheduling.

**This is not a real exchange.** There is no networking, no FIX protocol, no market connectivity. Orders are generated synthetically by the gateway and processed in-process. The goal is to study and measure the performance of core matching engine data structures and lock-free communication under realistic load patterns.

---

## Architecture

Three threads communicate exclusively through lock-free SPSC queues — no mutexes, no condition variables on the hot path.

```
Gateway Thread                Engine Thread              Snapshot Thread
──────────────────            ──────────────────         ──────────────────
Synthetic order gen           Price-level array          Consumes snapshot
lognormal prices      ──────► intrusive linked    ──────► queue from engine
discrete_distribution  SPSC   lists per level      SPSC   Serializes book
Tracks live bids/asks  queue  65K direct-index     queue  state to JSON
                              order lookup table
                       ◄──────
                       ACK / fill events
```

**Matching engine internals:**
- **Order book:** array of price levels, each backed by an intrusive doubly-linked list of resting orders
- **Order storage:** 65,535-slot direct-index table — order ID maps directly to the order node, no hashing
- **Best bid/ask tracking:** bitmaps with `TZCNT`/`LZCNT` for O(1) BBO lookup
- **ID recycling:** free-list stack; IDs are immediately reusable after a fill or cancel

**Gateway:**
- Generates orders using `std::lognormal_distribution` (prices) and `std::discrete_distribution` (order types)
- Tracks live bids/asks per order ID to issue correct cancels and avoid phantom cancels on already-filled orders
- Configurable total order count and target rate

**Snapshot writer:**
- Reads engine state through a dedicated SPSC queue, never directly — avoids data races without touching the hot path
- Serializes order book depth to JSON for the frontend visualizer

---

## Results

Benchmarked on a **Dell Precision 3490** (Intel Core Ultra 7 165H) running Linux Mint with CPU isolation and real-time scheduling.

| Metric | Value |
|---|---|
| Engine p50 latency | ~19 ns |
| Gateway throughput | 7M+ orders/sec |
| Total orders (benchmark run) | 150,000,000 |
| Engine core | 1 (`SCHED_FIFO`, priority 99) |
| Gateway core | 2 (`SCHED_FIFO`, priority 98) |

Latency is measured as the round-trip time from order enqueue on the gateway side to ACK/fill dequeue back on the gateway side, using `CLOCK_MONOTONIC`. Numbers are taken after a warm-up phase. This is a laptop-class machine — a server with a higher base clock and no power management interference would improve these figures.

---

## Building

Requires g++, C++20, and pthreads. No external dependencies.

```bash
git clone https://github.com/TheKairavPatel/Low-Latency-Exchange-System
cd Low-Latency-Exchange-System
mkdir build

# Latency + throughput benchmark
make linux-bench

# Live demo with order book visualizer
make live-demo
```

Compile flags used: `-O3 -march=native -std=c++20 -flto -funroll-loops -fno-exceptions -fno-rtti`

`-march=native` enables `TZCNT`/`LZCNT` and other BMI intrinsics. Build and run on the same machine.

---

## Running on Linux

The benchmark pins threads to specific cores and sets real-time scheduling. To get clean results you want those cores isolated from the OS scheduler before you run.

### Isolate CPU cores

Edit your GRUB config:

```bash
sudo nano /etc/default/grub
```

Add `isolcpus`, `nohz_full`, and `rcu_nocbs` to the kernel command line:

```
GRUB_CMDLINE_LINUX_DEFAULT="quiet splash isolcpus=1,2 nohz_full=1,2 rcu_nocbs=1,2"
```

Then update and reboot:

```bash
sudo update-grub && sudo reboot
```

Verify it worked:

```bash
cat /sys/devices/system/cpu/isolated
# should print: 1-2
```

### Run the benchmark

The benchmark requires `CAP_SYS_NICE` to set `SCHED_FIFO`. Either run as root or grant the capability to the binary:

```bash
sudo setcap cap_sys_nice+ep build/LinuxBench
./build/LinuxBench
```

For cleanest results, also set the CPU governor to performance mode before running:

```bash
sudo cpupower frequency-set -g performance
```

---

## Configurable parameters

In `bench/LinuxBench.cpp`:

```cpp
Engine engine(74000, running);
// 74000 = base price in ticks

Gateway gw(engine, running, false, 150'000'000, 100'000'000);
// args: engine ref, shutdown flag, verbose, total orders, target orders/sec
```

The target rate (100M/sec) is intentionally above realistic capacity — it puts the engine under maximum queue pressure so throughput is bottlenecked by matching speed, not order generation speed.

Thread pinning and priorities are set in `main()`:

```cpp
pinThread(engineThread, 1);   // bind engine to core 1
pinThread(gatewayThread, 2);  // bind gateway to core 2
setFIFO(engineThread, 99);    // engine: highest priority
setFIFO(gatewayThread, 98);   // gateway: one below
```

Change the core numbers to match whichever cores you isolated.
