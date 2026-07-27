#include "trading/core/runtime.hpp"

#include "trading/core/affinity.hpp"
#include "trading/core/logger.hpp"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <utility>

namespace trading {

Runtime::Runtime(RuntimeConfig cfg)
    : cfg_(std::move(cfg)),
      strategy_(cfg_.strategy),
      breaker_(cfg_.circuitErrorThreshold, cfg_.circuitWindowMsgs) {
    strategy_.setSymbol(cfg_.symbolId, cfg_.tick_size);
    strategy_.setInitialCash(cfg_.initial_cash);
    risk_.configure(cfg_.symbolId, cfg_.max_position, cfg_.initial_cash, cfg_.tick_size);

    cfg_.ws.product_ids = {cfg_.product_id};
    cfg_.ws.coreId = cfg_.networkCoreId;
    ws_ = std::make_unique<CoinbaseWSClient>(cfg_.ws, CoinbaseAuth::fromEnv());
    ws_->setMessageCallback(
        [this](const CoinbaseMessage& m, Timestamp ts) { onCoinbaseMessage(m, ts); });
    ws_->setParseErrorCallback([this] { ++metrics_.parseErrors; });
    ws_->setReconnectCallback([this] {
        ++metrics_.reconnects;
        bookInvalidate_.store(true, std::memory_order_release);
    });
    publishPnl();
}

Runtime::~Runtime() {
    stop();
}

void Runtime::start() {
    if (started_.exchange(true)) return;
    running_.store(true, std::memory_order_release);
    strategyThread_ = std::thread([this] {
        pinThreadToCore(cfg_.strategyCoreId);
        strategyLoop();
    });
    ws_->start();
}

void Runtime::stop() {
    if (!started_.exchange(false)) return;
    if (ws_) ws_->stop();                              // stop the producer first
    running_.store(false, std::memory_order_release);  // then let the loop drain and exit
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
                ev.snap.bids[i] = {Price::fromDouble(msg.bids[i].price, cfg_.tick_size),
                                   Qty{msg.bids[i].qty}};
            }
            for (size_t i = 0; i < na; ++i) {
                ev.snap.asks[i] = {Price::fromDouble(msg.asks[i].price, cfg_.tick_size),
                                   Qty{msg.asks[i].qty}};
            }
            ev.snap.nBids = static_cast<uint32_t>(nb);
            ev.snap.nAsks = static_cast<uint32_t>(na);
            break;
        }
        case CoinbaseMsgType::L2Update: {
            ev.kind = MarketEvent::Kind::L2Update;
            // A level2_batch message can carry more changes than one event
            // holds; emit full chunks here and let the last fall through.
            size_t total = msg.changes.size();
            size_t off = 0;
            for (;;) {
                size_t n = std::min(total - off, size_t{128});
                for (size_t i = 0; i < n; ++i) {
                    ev.l2.changeSides[i] = msg.changes[off + i].side;
                    ev.l2.changePrices[i] = msg.changes[off + i].price;
                    ev.l2.changeQtys[i] = msg.changes[off + i].qty;
                }
                ev.l2.nChanges = static_cast<uint32_t>(n);
                off += n;
                if (off >= total) break;
                if (!queue_.try_push(ev)) ++metrics_.queueFullDrops;
            }
            break;
        }
        case CoinbaseMsgType::Heartbeat: {
            ev.kind = MarketEvent::Kind::Heartbeat;
            ++metrics_.heartbeatsRecv;
            break;
        }
        default: ++metrics_.msgsReceived; return;
    }

    ++metrics_.msgsReceived;
    bool pushed = queue_.try_push(ev);
    if (!pushed && ev.kind == MarketEvent::Kind::Snapshot) {
        // Losing a snapshot leaves the book unsynced until the next reconnect;
        // retry briefly before giving up.
        for (int i = 0; i < 200 && !pushed; ++i) {
            std::this_thread::sleep_for(std::chrono::microseconds(100));
            pushed = queue_.try_push(ev);
        }
    }
    if (!pushed) ++metrics_.queueFullDrops;
    metrics_.queueDepth.recordNs(static_cast<int64_t>(queue_.size()));
}

void Runtime::strategyLoop() noexcept {
    MarketEvent ev;
    while (running_.load(std::memory_order_acquire)) {
        // Strategy state is reset here, not in the reconnect callback, which
        // runs on the network thread.
        if (bookInvalidate_.exchange(false, std::memory_order_acq_rel)) {
            bookReady_ = false;
            strategy_.onReconnect();
        }
        if (!queue_.try_pop(ev)) {
            if (cfg_.strategyBusySpin) continue;
            std::this_thread::sleep_for(std::chrono::microseconds(cfg_.strategySleepUs));
            continue;
        }
        processEvent(ev);
    }
    while (queue_.try_pop(ev))
        processEvent(ev);
}

void Runtime::processEvent(const MarketEvent& ev) noexcept {
    breaker_.recordMessage();
    auto bookStart = Clock::now();

    switch (ev.kind) {
        case MarketEvent::Kind::Snapshot: {
            book_.clear();
            // Anchor the window on the snapshot mid, not on whichever level lands first.
            int64_t bestBid = INT64_MIN;
            int64_t bestAsk = INT64_MAX;
            for (uint32_t i = 0; i < ev.snap.nBids; ++i) {
                if (ev.snap.bids[i].qty.value() > 0.0)
                    bestBid = std::max(bestBid, ev.snap.bids[i].price.ticks());
            }
            for (uint32_t i = 0; i < ev.snap.nAsks; ++i) {
                if (ev.snap.asks[i].qty.value() > 0.0)
                    bestAsk = std::min(bestAsk, ev.snap.asks[i].price.ticks());
            }
            if (bestBid != INT64_MIN && bestAsk != INT64_MAX) {
                book_.seedReference(Price::fromTicks(bestBid + (bestAsk - bestBid) / 2));
            }
            for (uint32_t i = 0; i < ev.snap.nBids; ++i) {
                book_.update(Side::Bid, ev.snap.bids[i].price, ev.snap.bids[i].qty);
            }
            for (uint32_t i = 0; i < ev.snap.nAsks; ++i) {
                book_.update(Side::Ask, ev.snap.asks[i].price, ev.snap.asks[i].qty);
            }
            bookReady_ = true;
            break;
        }
        case MarketEvent::Kind::L2Update: {
            if (!bookReady_) {
                ++metrics_.preSnapshotDrops;
                return;
            }
            for (uint32_t i = 0; i < ev.l2.nChanges; ++i) {
                book_.update(ev.l2.changeSides[i],
                             Price::fromDouble(ev.l2.changePrices[i], cfg_.tick_size),
                             Qty{ev.l2.changeQtys[i]});
            }
            break;
        }
        case MarketEvent::Kind::Heartbeat: return;
    }

    auto bookDone = Clock::now();
    metrics_.parseToBook.recordNs((bookDone - bookStart).count());

    if (auto bid = book_.topOfBook(Side::Bid)) {
        markBid_.store(bid->price.toDouble(cfg_.tick_size), std::memory_order_relaxed);
    }
    if (auto ask = book_.topOfBook(Side::Ask)) {
        markAsk_.store(ask->price.toDouble(cfg_.tick_size), std::memory_order_relaxed);
    }

    Signal sig = strategy_.onMarketUpdate(book_, bookDone);
    auto signalDone = Clock::now();
    metrics_.bookToSignal.recordNs((signalDone - bookDone).count());
    metrics_.tickToSignal.recordNs((signalDone - ev.recvNs).count());

    if (sig.isActionable()) {
        ++metrics_.signalsGenerated;
        dispatchOrder(sig, signalDone);
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

    if (breaker_.isTripped()) {
        ++metrics_.circuitBlocked;
        return;
    }

    if (!risk_.approve(o)) {
        ++metrics_.ordersRejected;
        breaker_.recordError();
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
        publishPnl();
    }
}

void Runtime::publishPnl() noexcept {
    pnlCash_.store(strategy_.cash(), std::memory_order_relaxed);
    pnlPosition_.store(strategy_.position(), std::memory_order_relaxed);
    pnlAvgEntry_.store(strategy_.avgEntry(), std::memory_order_relaxed);
    pnlRealized_.store(strategy_.realized(), std::memory_order_relaxed);
}

}  // namespace trading
