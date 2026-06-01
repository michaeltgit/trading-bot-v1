#include "trading/core/logger.hpp"
#include "trading/core/result.hpp"
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

Result<RuntimeConfig> loadConfig(const std::string& path) {
    std::ifstream in(path);
    if (!in.is_open()) return Error::ConfigFileNotFound;

    json j;
    try {
        in >> j;
    } catch (const std::exception&) {
        return Error::ParseMalformedJson;
    }

    RuntimeConfig cfg;
    cfg.product_id = j.value("product_id", "BTC-USD");
    int symbolId = j.value("symbol_id", 0);
    cfg.symbolId = static_cast<SymbolId>(symbolId);
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
        cfg.strategy.depth_levels = s.value("depth_levels", size_t{5});
        cfg.strategy.smoothing_window = s.value("smoothing_window", size_t{5});
        cfg.strategy.entry_imbalance = s.value("entry_imbalance", 0.30);
        cfg.strategy.take_profit_bps = s.value("take_profit_bps", 8.0);
        cfg.strategy.stop_loss_bps = s.value("stop_loss_bps", 8.0);
        cfg.strategy.max_spread_bps = s.value("max_spread_bps", 5.0);
        cfg.strategy.trade_fraction = s.value("trade_fraction", 0.10);
        cfg.strategy.cooldown_s = s.value("cooldown_s", 2.0);
        cfg.strategy.allow_short = s.value("allow_short", true);
    }

    if (j.contains("risk")) {
        const auto& r = j["risk"];
        cfg.circuitErrorThreshold = r.value("circuit_error_threshold", cfg.circuitErrorThreshold);
        cfg.circuitWindowMsgs = r.value("circuit_window_msgs", cfg.circuitWindowMsgs);
    }

    if (j.contains("venue")) {
        const auto& v = j["venue"];
        cfg.ws.host = v.value("host", cfg.ws.host);
        cfg.ws.port = v.value("port", cfg.ws.port);
        cfg.ws.channel = v.value("channel", cfg.ws.channel);
    }

    if (cfg.tick_size <= 0.0) return Error::ConfigInvalidValue;
    if (cfg.max_position <= 0.0) return Error::ConfigInvalidValue;
    if (cfg.initial_cash < 0.0) return Error::ConfigInvalidValue;
    if (symbolId < 0 || static_cast<size_t>(symbolId) >= MAX_SYMBOLS) return Error::ConfigInvalidValue;
    if (cfg.strategy.depth_levels == 0 || cfg.strategy.smoothing_window == 0) return Error::ConfigInvalidValue;
    if (cfg.strategy.trade_fraction <= 0.0 || cfg.strategy.trade_fraction > 1.0) return Error::ConfigInvalidValue;
    if (cfg.strategy.entry_imbalance < 0.0 || cfg.strategy.entry_imbalance > 1.0) return Error::ConfigInvalidValue;
    if (cfg.strategy.take_profit_bps <= 0.0 || cfg.strategy.stop_loss_bps <= 0.0) return Error::ConfigInvalidValue;
    if (cfg.ws.host.empty() || cfg.ws.port.empty()) return Error::ConfigInvalidValue;

    return cfg;
}

} // namespace

int main(int argc, char** argv) {
    std::signal(SIGINT, handleSignal);
    std::signal(SIGTERM, handleSignal);

    std::string cfgPath = (argc >= 2) ? argv[1] : "config.json";

    auto cfgResult = loadConfig(cfgPath);
    if (!cfgResult) {
        std::cerr << "Failed to load config (" << cfgPath << "): "
                  << toString(cfgResult.error()) << "\n";
        return 1;
    }
    RuntimeConfig cfg = std::move(cfgResult).value();

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
            double pos = rt.pnlPosition();
            std::cerr << "[heartbeat] msgs=" << m.msgsReceived.load()
                      << " signals=" << m.signalsGenerated.load()
                      << " fills=" << m.fills.load()
                      << " rej=" << m.ordersRejected.load()
                      << " cash=" << rt.pnlCash()
                      << " pos=" << pos
                      << " realized=" << rt.pnlRealized();
            if (pos != 0.0) std::cerr << " unrealized=" << rt.pnlUnrealized();
            std::cerr << "\n";
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
              << " realized=" << rt.pnlRealized();
    if (rt.pnlPosition() != 0.0) std::cerr << " unrealized=" << rt.pnlUnrealized();
    std::cerr << "\n";

    Logger::instance().stop();
    return 0;
}
