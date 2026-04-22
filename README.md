# trading-engine

A low-latency C++ trading engine against Coinbase Exchange. Paper trading only; no real orders are placed.

The network thread ingests the Coinbase WebSocket feed, parses messages with simdjson, and pushes structured market events onto a lock-free SPSC queue. The strategy thread drains the queue, updates a flat price-level order book, runs the imbalance strategy, and routes signals through a risk check and a simulated fill engine. HDR latency histograms are recorded at every stage.

## Build

```
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
```

Build types are Debug (ASAN + UBSAN at -O1), Release (-O3, -march=native, LTO), and RelWithDebInfo.

Dependencies: C++20 compiler, CMake 3.20+, Boost (system, thread), OpenSSL, nlohmann_json, libcurl, spdlog. simdjson and Catch2 are pulled in via FetchContent.

## Run

```
./build/trading_engine config.json
```

Ctrl-C flushes metrics and exits cleanly. A heartbeat line prints every 2 seconds and a full metrics dump every 30 seconds.

## Tests

```
cmake --build build --target tests_cpp
ctest --test-dir build --output-on-failure

./build/tests_cpp "[!benchmark]"
```

## Configuration

`config.json`:

```
{
  "product_id": "BTC-USD",
  "symbol_id": 0,
  "tick_size": 0.01,
  "max_position": 5.0,
  "initial_cash": 10000.0,
  "venue": {
    "host": "ws-feed.exchange.coinbase.com",
    "port": "443",
    "channel": "level2_batch"
  },
  "threading": {
    "network_core": -1,
    "strategy_core": -1,
    "strategy_busy_spin": false
  },
  "strategy": {
    "depth_levels": 6,
    "rolling_window": 5,
    "buy":  { "imbalance_pct": 75.0, "spread_pct": 0.0003, "cooldown_s": 20.0, "max_fraction": 0.10 },
    "sell": { "imbalance_pct": 25.0, "spread_pct": 0.0003, "cooldown_s": 10.0, "max_fraction": 1.0 }
  }
}
```

`channel` defaults to `level2_batch` (public, ~50ms batched). Switch to `level2` and set `COINBASE_API_KEY` / `COINBASE_API_SECRET` / `COINBASE_API_PASSPHRASE` to use the authenticated stream.

Set `network_core` / `strategy_core` to pin threads to specific CPU cores on Linux (via `pthread_setaffinity_np`) or macOS (best-effort `thread_policy_set`). `strategy_busy_spin` hot-waits on the queue instead of sleeping; only useful with pinned, isolated cores.

## Layout

```
include/trading/
  core/       types, result, clock, logger, metrics, runtime
  md/         coinbase_*, bounded_book
  strategy/   signal, imbalance_strategy, order_sizer, pnl_tracker
  exec/       order_types, risk_manager, execution_engine, circuit_breaker
  util/       spsc_queue, object_pool, hdr_histogram
src/
tests/        Catch2 tests + fixtures/
config.json
CMakeLists.txt
```

## Strategy

Dollar-weighted order book imbalance over the top N levels, with a rolling window for trend confirmation and a spread filter. Order size is confidence-scaled by how strong the imbalance is minus a spread penalty. All parameters live in `config.json`.

This is a taker strategy. It crosses the spread on every trade, so in simulation it pays the half-spread each round trip. Replace `ImbalanceStrategy` with a maker / quoting strategy to get positive expectancy; the rest of the engine is venue- and signal-agnostic.

## CI

GitHub Actions runs Ubuntu and macOS in Debug and Release, a dedicated TSAN job, and a clang-format check.
