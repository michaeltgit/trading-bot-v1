#include "trading/core/logger.hpp"
#include "trading/core/runtime.hpp"

#include <nlohmann/json.hpp>

#include <atomic>
#include <chrono>
#include <csignal>
#include <fstream>
#include <iostream>
#include <string>
#include <thread>

using namespace trading;
using nlohmann::json;

namespace {
std::atomic<bool> g_running{true};

void handleSignal(int) {
    g_running.store(false, std::memory_order_release);
}

RuntimeConfig loadConfig(const std::string& path) {
    std::ifstream in(path);
    if (!in.is_open()) {
        throw std::runtime_error("cannot open " + path);
    }
    json j;
    in >> j;

    RuntimeConfig cfg;
    cfg.product_id = j.value("product_id", "BTC-USD");
    cfg.symbolId = static_cast<SymbolId>(j.value("symbol_id", 0));
    cfg.tick_size = j.value("tick_size", 0.01);
    cfg.max_position = j.value("max_position", 5.0);
    cfg.initial_cash = j.value("initial_cash", 10'000.0);

    if (j.contains("threading")) {
        const auto& t = j["threading"];
        cfg.networkCoreId = t.value("network_core", -1);
        cfg.strategyCoreId = t.value("strategy_core", -1);
        cfg.strategyBusySpin = t.value("strategy_busy_spin", false);
    }

    if (j.contains("strategy")) {
        const auto& s = j["strategy"];
        cfg.strategy.depth_levels = s.value("depth_levels", size_t{6});
        cfg.strategy.rolling_window = s.value("rolling_window", size_t{5});
        if (s.contains("buy")) {
            const auto& b = s["buy"];
            cfg.strategy.buy_imbalance_pct = b.value("imbalance_pct", 75.0);
            cfg.strategy.buy_spread_pct = b.value("spread_pct", 0.0003);
            cfg.strategy.buy_cooldown_s = b.value("cooldown_s", 20.0);
            cfg.strategy.buy_max_fraction = b.value("max_fraction", 0.10);
        }
        if (s.contains("sell")) {
            const auto& s2 = s["sell"];
            cfg.strategy.sell_imbalance_pct = s2.value("imbalance_pct", 25.0);
            cfg.strategy.sell_spread_pct = s2.value("spread_pct", 0.0003);
            cfg.strategy.sell_cooldown_s = s2.value("cooldown_s", 10.0);
            cfg.strategy.sell_max_fraction = s2.value("max_fraction", 1.0);
        }
    }

    if (j.contains("venue")) {
        const auto& v = j["venue"];
        cfg.ws.host = v.value("host", cfg.ws.host);
        cfg.ws.port = v.value("port", cfg.ws.port);
        cfg.ws.channel = v.value("channel", cfg.ws.channel);
    }

    return cfg;
}

} // namespace

int main(int argc, char** argv) {
    std::signal(SIGINT, handleSignal);
    std::signal(SIGTERM, handleSignal);

    std::string cfgPath = (argc >= 2) ? argv[1] : "config.json";

    RuntimeConfig cfg;
    try {
        cfg = loadConfig(cfgPath);
    } catch (const std::exception& e) {
        std::cerr << "Failed to load config: " << e.what() << "\n";
        return 1;
    }

    TRADING_LOG_INFO("trading-engine starting");

    Runtime rt(cfg);
    rt.start();

    auto lastReport = std::chrono::steady_clock::now();
    auto lastHeartbeat = lastReport;
    while (g_running.load(std::memory_order_acquire)) {
        std::this_thread::sleep_for(std::chrono::milliseconds(200));

        auto now = std::chrono::steady_clock::now();
        if (now - lastHeartbeat >= std::chrono::seconds(2)) {
            const auto& m = rt.metrics();
            std::cerr << "[heartbeat] msgs=" << m.msgsReceived.load()
                      << " signals=" << m.signalsGenerated.load()
                      << " fills=" << m.fills.load()
                      << " rej=" << m.ordersRejected.load()
                      << " cash=" << rt.pnlCash()
                      << " pos=" << rt.pnlPosition()
                      << " realized=" << rt.pnlRealized() << "\n";
            lastHeartbeat = now;
        }
        if (now - lastReport >= std::chrono::seconds(30)) {
            std::cerr << "--- periodic metrics ---\n";
            rt.metrics().dump(std::cerr);
            lastReport = now;
        }
    }

    TRADING_LOG_INFO("shutdown requested; stopping runtime");
    rt.stop();

    std::cerr << "=== final metrics ===\n";
    rt.metrics().dump(std::cerr);
    std::cerr << "pnl: cash=" << rt.pnlCash()
              << " position=" << rt.pnlPosition()
              << " realized=" << rt.pnlRealized() << "\n";

    Logger::instance().stop();
    return 0;
}
