#pragma once

#include <atomic>
#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

#include "trading/execution_engine.hpp"
#include "trading/limit_order_book.hpp"
#include "trading/market_data_connector.hpp"
#include "trading/new_order.hpp"
#include "trading/risk_manager.hpp"

namespace trading {

class SymbolWorker {
public:
    SymbolWorker(std::string symbol, int maxPosition);
    ~SymbolWorker();

    void start();
    void join();
    void stop();

    LimitOrderBook& getOrderBook();
    void sendOrder(const NewOrder& order);
    std::string getSymbol() const;

    void setCallback(std::function<void(const ExecutionReport&)> cb);
    void setDataCallback(std::function<void()> cb);

private:
    void run();

    std::string symbol_;
    std::thread thread_;
    std::atomic<bool> running_{false};

    MarketDataConnector mdc_;
    ExecutionEngine engine_;
    RiskManager riskManager_;

    std::function<void(const ExecutionReport&)> callback_;
    std::function<void()> dataCb_;

    std::mutex mtx_;
    std::condition_variable cv_;
};

} // namespace trading