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
    "depth_levels": 5,
    "smoothing_window": 5,
    "entry_imbalance": 0.30,
    "take_profit_bps": 8.0,
    "stop_loss_bps": 8.0,
    "max_spread_bps": 5.0,
    "trade_fraction": 0.10,
    "cooldown_s": 2.0,
    "allow_short": true
  }
}
```

`channel` defaults to `level2_batch` (public, ~50ms batched). Switch to `level2` and set `COINBASE_API_KEY` / `COINBASE_API_SECRET` / `COINBASE_API_PASSPHRASE` to use the authenticated stream. The TLS connection verifies the server certificate and hostname.

An optional `risk` block tunes the circuit breaker, which halts order dispatch after `circuit_error_threshold` rejected orders inside a `circuit_window_msgs` message window and auto-resets after a clean window:

```
"risk": { "circuit_error_threshold": 20, "circuit_window_msgs": 500 }
```

`l2update`s received before the first `snapshot` (or before a post-reconnect snapshot) are dropped rather than applied to an unsynced book; on reconnect the feed re-sends a fresh snapshot.

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

Directional order-book-imbalance momentum. Each update computes the size imbalance over the top N levels (`(bid − ask) / (bid + ask)`, in `[-1, 1]`), smoothed over a short window. When flat, a strong smoothed imbalance opens a position in its direction — long if bid-heavy, short if ask-heavy — but only if the spread is tight and the cooldown has elapsed. While holding, the position is closed only on a take-profit or stop-loss move (in bps of the entry price); it does not round-trip on every reversion. One position at a time. All parameters live in `config.json`.

It's still a taker (it crosses the spread to enter and exit), so each round trip pays roughly the spread. Holding for a take-profit several times larger than the spread is what keeps that cost from dominating — tight brackets degrade toward paying the spread on every trade, wide brackets hold for real moves. Expectancy is regime-dependent: it makes money when imbalance predicts the next move and takes capped losses otherwise. For consistent positive expectancy you'd want a maker / quoting strategy that earns the spread instead of paying it; the rest of the engine is venue- and signal-agnostic.

## CI

GitHub Actions runs Ubuntu and macOS in Debug and Release, a dedicated TSAN job, and a clang-format check.
