#pragma once

#include "trading/core/clock.hpp"
#include "trading/core/metrics.hpp"
#include "trading/core/types.hpp"
#include "trading/exec/circuit_breaker.hpp"
#include "trading/exec/execution_engine.hpp"
#include "trading/exec/risk_manager.hpp"
#include "trading/md/bounded_book.hpp"
#include "trading/md/coinbase_messages.hpp"
#include "trading/md/coinbase_ws_client.hpp"
#include "trading/strategy/imbalance_strategy.hpp"
#include "trading/util/spsc_queue.hpp"

#include <atomic>
#include <memory>
#include <string>
#include <thread>
#include <vector>

namespace trading {

struct RuntimeConfig {
    std::string product_id = "BTC-USD";
    SymbolId symbolId = 0;
    double tick_size = 0.01;
    double max_position = 5.0;
    double initial_cash = 10'000.0;

    StrategyParams strategy;

    int networkCoreId = -1;
    int strategyCoreId = -1;
    bool strategyBusySpin = false;
    int strategySleepUs = 50;

    uint64_t circuitErrorThreshold = 20;
    uint64_t circuitWindowMsgs = 500;

    CoinbaseWSConfig ws;
};

struct MarketEvent {
    enum class Kind : uint8_t { Snapshot, L2Update, Heartbeat };
    struct Snap {
        uint32_t nBids;
        uint32_t nAsks;
        PriceLevel bids[64];
        PriceLevel asks[64];
    };
    struct L2 {
        uint32_t nChanges;
        Side changeSides[128];
        double changePrices[128];
        double changeQtys[128];
    };
    Kind kind;
    Timestamp recvNs;
    union {
        Snap snap;
        L2 l2;
    };

    MarketEvent() noexcept : kind(Kind::Heartbeat), recvNs{}, snap{} {}
};

class Runtime {
public:
    explicit Runtime(RuntimeConfig cfg);
    ~Runtime();
    Runtime(const Runtime&) = delete;
    Runtime& operator=(const Runtime&) = delete;

    void start();
    void stop();

    const Metrics& metrics() const noexcept { return metrics_; }
    double pnlRealized() const noexcept { return pnlRealized_.load(std::memory_order_relaxed); }
    double pnlCash() const noexcept { return pnlCash_.load(std::memory_order_relaxed); }
    double pnlPosition() const noexcept { return pnlPosition_.load(std::memory_order_relaxed); }
    double pnlUnrealized() const noexcept {
        double pos = pnlPosition_.load(std::memory_order_relaxed);
        if (pos == 0.0) return 0.0;
        double mark = (pos > 0.0 ? markBid_ : markAsk_).load(std::memory_order_relaxed);
        if (mark <= 0.0) return 0.0;
        return pos * (mark - pnlAvgEntry_.load(std::memory_order_relaxed));
    }

private:
    void onCoinbaseMessage(const CoinbaseMessage& msg, Timestamp recvTs) noexcept;
    void strategyLoop() noexcept;
    void processEvent(const MarketEvent& ev) noexcept;
    void dispatchOrder(const Signal& sig, Timestamp now) noexcept;
    void publishPnl() noexcept;

    RuntimeConfig cfg_;
    Metrics metrics_;

    std::unique_ptr<CoinbaseWSClient> ws_;

    BoundedBook<4096> book_;
    ImbalanceStrategy strategy_;
    RiskManager risk_;
    ExecutionEngine engine_;
    CircuitBreaker breaker_;

    SPSCQueue<MarketEvent, 4096> queue_;

    std::thread strategyThread_;
    std::atomic<bool> started_{false};
    std::atomic<bool> running_{false};
    std::atomic<bool> bookInvalidate_{false};
    bool bookReady_ = false;

    std::atomic<double> markBid_{0.0};
    std::atomic<double> markAsk_{0.0};

    // PnL snapshots for the main thread's heartbeat; the strategy's own state
    // is touched only on the strategy thread.
    std::atomic<double> pnlCash_{0.0};
    std::atomic<double> pnlPosition_{0.0};
    std::atomic<double> pnlAvgEntry_{0.0};
    std::atomic<double> pnlRealized_{0.0};

    OrderId nextOrderId_ = 1;
};

}  // namespace trading
