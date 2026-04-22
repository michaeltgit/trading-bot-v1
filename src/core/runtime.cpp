#include "trading/core/runtime.hpp"

#include "trading/core/logger.hpp"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <utility>

#if defined(__linux__)
#include <pthread.h>
#include <sched.h>
#elif defined(__APPLE__)
#include <mach/mach.h>
#include <mach/thread_policy.h>
#include <pthread.h>
#endif

namespace trading {

Runtime::Runtime(RuntimeConfig cfg)
    : cfg_(std::move(cfg)),
      book_(cfg_.tick_size),
      strategy_(cfg_.strategy) {
    strategy_.setSymbol(cfg_.symbolId, cfg_.tick_size);
    strategy_.setInitialCash(cfg_.initial_cash);
    risk_.configure(cfg_.symbolId, cfg_.max_position);

    cfg_.ws.product_ids = {cfg_.product_id};
    ws_ = std::make_unique<CoinbaseWSClient>(cfg_.ws, CoinbaseAuth::fromEnv());
    ws_->setMessageCallback([this](const CoinbaseMessage& m, Timestamp ts) {
        onCoinbaseMessage(m, ts);
    });
    ws_->setReconnectCallback([this] {
        ++metrics_.reconnects;
        strategy_.onReconnect();
    });
}

Runtime::~Runtime() {
    stop();
}

void Runtime::start() {
    if (running_.exchange(true)) return;
    strategyThread_ = std::thread([this] {
        if (cfg_.strategyCoreId >= 0) pinThreadToCore(cfg_.strategyCoreId);
        strategyLoop();
    });
    ws_->start();
}

void Runtime::stop() {
    if (!running_.exchange(false)) return;
    if (ws_) ws_->stop();
    if (strategyThread_.joinable()) strategyThread_.join();
}

void Runtime::onCoinbaseMessage(const CoinbaseMessage& msg, Timestamp recvTs) noexcept {
    auto parseDone = Clock::now();
    metrics_.wireToParse.recordNs((parseDone - recvTs).count());

    MarketEvent ev{};
    ev.recvNs = recvTs;

    switch (msg.type) {
        case CoinbaseMsgType::Snapshot: {
            ev.kind = MarketEvent::Kind::Snapshot;
            size_t nb = std::min(msg.bids.size(), size_t{64});
            size_t na = std::min(msg.asks.size(), size_t{64});
            for (size_t i = 0; i < nb; ++i) {
                ev.bids[i] = {Price::fromDouble(msg.bids[i].price, cfg_.tick_size),
                              Qty{msg.bids[i].qty}};
            }
            for (size_t i = 0; i < na; ++i) {
                ev.asks[i] = {Price::fromDouble(msg.asks[i].price, cfg_.tick_size),
                              Qty{msg.asks[i].qty}};
            }
            ev.nBids = static_cast<uint32_t>(nb);
            ev.nAsks = static_cast<uint32_t>(na);
            break;
        }
        case CoinbaseMsgType::L2Update: {
            ev.kind = MarketEvent::Kind::L2Update;
            size_t n = std::min(msg.changes.size(), size_t{128});
            for (size_t i = 0; i < n; ++i) {
                ev.changeSides[i] = msg.changes[i].side;
                ev.changePrices[i] = msg.changes[i].price;
                ev.changeQtys[i] = msg.changes[i].qty;
            }
            ev.nChanges = static_cast<uint32_t>(n);
            break;
        }
        case CoinbaseMsgType::Heartbeat: {
            ev.kind = MarketEvent::Kind::Heartbeat;
            ++metrics_.heartbeatsRecv;
            break;
        }
        default:
            ++metrics_.msgsReceived;
            return;
    }

    ++metrics_.msgsReceived;
    if (!queue_.try_push(ev)) {
        ++metrics_.queueFullDrops;
    }
    metrics_.queueDepth.recordNs(static_cast<int64_t>(queue_.size()));
}

void Runtime::strategyLoop() noexcept {
    MarketEvent ev;
    while (running_.load(std::memory_order_acquire)) {
        if (!queue_.try_pop(ev)) {
            if (cfg_.strategyBusySpin) {
                continue;
            }
            std::this_thread::sleep_for(std::chrono::microseconds(50));
            continue;
        }

        auto bookStart = Clock::now();

        switch (ev.kind) {
            case MarketEvent::Kind::Snapshot: {
                book_.clear();
                for (uint32_t i = 0; i < ev.nBids; ++i) {
                    book_.update(Side::Bid, ev.bids[i].price, ev.bids[i].qty);
                }
                for (uint32_t i = 0; i < ev.nAsks; ++i) {
                    book_.update(Side::Ask, ev.asks[i].price, ev.asks[i].qty);
                }
                break;
            }
            case MarketEvent::Kind::L2Update: {
                for (uint32_t i = 0; i < ev.nChanges; ++i) {
                    book_.update(ev.changeSides[i],
                                 Price::fromDouble(ev.changePrices[i], cfg_.tick_size),
                                 Qty{ev.changeQtys[i]});
                }
                break;
            }
            case MarketEvent::Kind::Heartbeat:
                continue;
        }

        auto bookDone = Clock::now();
        metrics_.parseToBook.recordNs((bookDone - bookStart).count());

        Signal sig = strategy_.onMarketUpdate(book_, bookDone);
        auto signalDone = Clock::now();
        metrics_.bookToSignal.recordNs((signalDone - bookDone).count());
        metrics_.tickToSignal.recordNs((signalDone - ev.recvNs).count());

        if (sig.isActionable()) {
            ++metrics_.signalsGenerated;
            dispatchOrder(sig, signalDone);
        }
    }
}

void Runtime::dispatchOrder(const Signal& sig, Timestamp now) noexcept {
    NewOrder o{};
    o.id = nextOrderId_++;
    o.symbolId = cfg_.symbolId;
    o.side = (sig.action == SignalAction::Buy) ? Side::Bid : Side::Ask;
    o.price = sig.price;
    o.qty = sig.qty;
    o.createdNs = now;

    ++metrics_.ordersSent;

    if (!risk_.approve(o)) {
        ++metrics_.ordersRejected;
        return;
    }
    ++metrics_.ordersApproved;

    auto orderDone = Clock::now();
    metrics_.signalToOrder.recordNs((orderDone - now).count());

    auto rpt = engine_.simulate(o, book_, cfg_.tick_size, Clock::now());
    auto fillDone = Clock::now();
    metrics_.orderToFill.recordNs((fillDone - orderDone).count());

    if (rpt.isFill) {
        ++metrics_.fills;
        if (rpt.execQty.value() < o.qty.value() - 1e-12) ++metrics_.partialFills;
        risk_.onFill(rpt);
        strategy_.onFill(rpt);
    }
}

void Runtime::pinThreadToCore(int core) noexcept {
#if defined(__linux__)
    cpu_set_t set;
    CPU_ZERO(&set);
    CPU_SET(core, &set);
    pthread_setaffinity_np(pthread_self(), sizeof(set), &set);
#elif defined(__APPLE__)
    thread_affinity_policy_data_t policy{core};
    thread_policy_set(pthread_mach_thread_np(pthread_self()),
                      THREAD_AFFINITY_POLICY,
                      reinterpret_cast<thread_policy_t>(&policy), 1);
#else
    (void)core;
#endif
}

} // namespace trading
