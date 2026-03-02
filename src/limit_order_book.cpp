#include "trading/limit_order_book.hpp"

#include <spdlog/spdlog.h>

#include <cmath>
#include <stdexcept>
#include <vector>

namespace trading {

static double normalizePrice(double p) {
    return std::round(p * 1e8) / 1e8;
}

LimitOrderBook::LimitOrderBook(const std::string& symbol)
    : symbol_(symbol), bids_(), asks_() {}

void LimitOrderBook::reset(const BookSnapshot& snapshot) {
    bids_.clear();
    asks_.clear();

    for (const auto& e : snapshot.bids) {
        if (e.size > 0.0) {
            bids_[normalizePrice(e.price)] = e.size;
        }
    }

    for (const auto& e : snapshot.asks) {
        if (e.size > 0.0) {
            asks_[normalizePrice(e.price)] = e.size;
        }
    }
}

void LimitOrderBook::resetTopLevels(const std::vector<OrderBookEntry>& bids,
                                    const std::vector<OrderBookEntry>& asks) {
    bids_.clear();
    asks_.clear();

    for (const auto& b : bids) {
        if (b.size > 0.0) {
            bids_[normalizePrice(b.price)] = b.size;
        }
    }

    for (const auto& a : asks) {
        if (a.size > 0.0) {
            asks_[normalizePrice(a.price)] = a.size;
        }
    }
}

void LimitOrderBook::onUpdate(Side side, double price, double size) {
    auto& book = (side == Side::Bid) ? bids_ : asks_;
    const double key = normalizePrice(price);

    if (size == 0.0) {
        book.erase(key);
        spdlog::info("[OrderBook] [{}] Removed {} at {:.2f}", symbol_, (side == Side::Bid ? "Bid" : "Ask"), price);
    } else {
        book[key] = size;
        spdlog::info("[OrderBook] [{}] Updated {} at {:.2f} to size {:.4f}", symbol_, (side == Side::Bid ? "Bid" : "Ask"), price, size);
    }
}

OrderBookEntry LimitOrderBook::topOfBook(Side side) const {
    const auto& book = (side == Side::Bid) ? bids_ : asks_;
    if (book.empty()) {
        throw std::runtime_error("empty book");
    }

    if (side == Side::Bid) {
        auto it = book.rbegin();
        return {it->first, it->second};
    } else {
        auto it = book.begin();
        return {it->first, it->second};
    }
}

std::vector<OrderBookEntry> LimitOrderBook::depth(Side side, size_t levels) const {
    const auto& book = (side == Side::Bid) ? bids_ : asks_;
    std::vector<OrderBookEntry> out;
    out.reserve(levels);

    if (side == Side::Bid) {
        for (auto it = book.rbegin(); it != book.rend() && out.size() < levels; ++it) {
            out.push_back({it->first, it->second});
        }
    } else {
        for (auto it = book.begin(); it != book.end() && out.size() < levels; ++it) {
            out.push_back({it->first, it->second});
        }
    }

    return out;
}

void LimitOrderBook::handleMarketDataUpdate(double price, double size, bool isBid) {
    onUpdate(isBid ? Side::Bid : Side::Ask, price, size);
}

} // namespace trading