#pragma once

#include "trading/core/clock.hpp"
#include "trading/core/metrics.hpp"
#include "trading/core/types.hpp"
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

    CoinbaseWSConfig ws;
};

struct MarketEvent {
    enum class Kind : uint8_t { Snapshot, L2Update, Heartbeat };
    Kind kind;
    Timestamp recvNs;
    uint32_t nChanges;
    PriceLevel bids[64];
    PriceLevel asks[64];
    uint32_t nBids;
    uint32_t nAsks;
    Side changeSides[128];
    double changePrices[128];
    double changeQtys[128];
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
    double pnlRealized() const noexcept { return strategy_.realized(); }
    double pnlCash() const noexcept { return strategy_.cash(); }
    double pnlPosition() const noexcept { return strategy_.position(); }

private:
    void onCoinbaseMessage(const CoinbaseMessage& msg, Timestamp recvTs) noexcept;
    void strategyLoop() noexcept;
    void dispatchOrder(const Signal& sig, Timestamp now) noexcept;
    static void pinThreadToCore(int core) noexcept;

    RuntimeConfig cfg_;
    Metrics metrics_;

    std::unique_ptr<CoinbaseWSClient> ws_;

    BoundedBook<4096> book_;
    ImbalanceStrategy strategy_;
    RiskManager risk_;
    ExecutionEngine engine_;

    SPSCQueue<MarketEvent, 4096> queue_;

    std::thread strategyThread_;
    std::atomic<bool> running_{false};

    OrderId nextOrderId_ = 1;
};

} // namespace trading
