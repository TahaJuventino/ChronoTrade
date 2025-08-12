# **ChronoTrade**

A multi-threaded RSI candlestick engine built with OS-level principles, security-first architecture, and red team simulation features.

---

## **📜 Overview**

ChronoTrade combines a hardened, modular financial engine with attack simulation layers and advanced observability.
Built for **real-time market processing**, **multi-indicator analytics**, and **adversarial testing**, the system follows a strict OS-style architecture with **thread safety**, **fault isolation**, and **security auditing**.

---

## **🗂 Modules**

* **`/core`** – Core types: `Order`, `Candlestick`, `AuthFlags` with invariant checks and safe constructors.
* **`/engine`** – Indicator engine (`RSI`, `MACD`, `SMA`, `BollingerBands`) with sliding window logic.
* **`/feed`** – CSV, socket, and shared memory feed sources with zero-copy parsing.
* **`/threads`** – ThreadPool, watchdog, and OS-level concurrency primitives.
* **`/event`** – EventBus pub/sub, anomaly injection hooks, and tamper-traced events.
* **`/security`** – Packet checksums, anomaly detection, and fuzzing entry points.
* **`/fuzz`** – libFuzzer integration, crash replay, corpus management, and mutation scripts.
* **`/tests`** – GoogleTest suites for unit, fuzz, and integration testing.
* **`/utils`** – Logger, panic handler, and time utilities.

---

## **📊 Current Progress – Phase 3**

### Feed System & Attack Simulation

1. **Unified Feed Interface** – Completed (`IFeedSource` with polymorphic dispatch).
2. **Thread Management** – `std::jthread`, `stop_token`, telemetry tracking in place.
3. **Real-Time Diagnostics** – JSON export, Prometheus `/metrics`, latency histograms.
4. **Red Team DSL** – Chaos DSL with time-based impairments integrated.
5. **Checksum Audit** – Framework ready; SHA-256 logging pending.
6. **Persistence & Replay** – Automatic logging and replay interfaces implemented.
7. **FeedFuzzer** – Full libFuzzer pipeline with crash replay.
8. **Edge Case Coverage** – Unicode, NaN, CRLF/LF mismatch, SHM crash simulation.

**Fuzzing Artifacts**:

| Path                                | Purpose                                       |
| ----------------------------------- | --------------------------------------------- |
| `fuzz/FeedFuzzer.cpp`               | libFuzzer entry point: fuzzes CSV/JSON parser |
| `fuzz/Corpus/valid/*.txt`           | Known-good CSV/JSON corpus                    |
| `fuzz/Corpus/invalid/*.txt`         | Mutated crash cases                           |
| `fuzz/Symbols/BacktraceLogger.hpp`  | Captures crashing call stack                  |
| `fuzz/Replay/FeedCrashReplayer.cpp` | Replays crash packets through full pipeline   |
| `fuzz/data/crash_packets.json`      | Stores crash data with metadata               |
| `fuzz/logs/crash_YYYYMMDD.dump`     | Full crash dump with telemetry                |
| `fuzz/scripts/mutate.py`            | Grammar-based corpus mutation                 |

---

## **📡 TCP Latency Proxy – Stage Progress**

**Stage 1 – Socket Core Max-Out** (75%)
Multi-threaded handling, token bucket throttling, event-driven core, zero-copy ring buffers, pluggable framing.
**Stage 2 – Chaos Injection Suite** (70%)
Latency, jitter, drop/dup rates, directional filtering, seeded reproducibility, Chaos DSL.
**Stage 3 – Observability** (60%)
Per-connection stats, Prometheus exporter, histograms, hexdump/PCAP.
**Stage 4 – Multi-Protocol Engine** (25%)
HTTP error handling, pluggable protocol framework.
**Stage 5 – Scale & Survivability** (55%)
Live config reload, watchdog, auto-restart, fuzz harness.

---

## **🛠 Build Requirements**

* MSYS2 with MinGW64
* GCC, CMake, Make
* Python 3 (for future ML module)
* GoogleTest (`pacman -S mingw-w64-x86_64-gtest` in MSYS2)

---

## **🔧 Build & Run**

```bash
# Open MSYS2 MINGW64 terminal
git clone https://github.com/TahaJuventino/ChronoTrade.git
cd ChronoTrade
mkdir build && cd build
cmake ..
make -j$(nproc)

# Run the engine
./chronotrade.exe

# Run all tests
ctest --output-on-failure

# Run fuzzer (example)
./fuzz_feedfuzzer -max_total_time=60
```

---

## **📜 Mission Statement**

> "A multi-threaded RSI candlestick engine built with OS-level principles, infused with red team simulation and financial logic."