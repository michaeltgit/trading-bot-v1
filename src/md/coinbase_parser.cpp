#include "trading/md/coinbase_parser.hpp"

#include <simdjson.h>

#include <cstdlib>
#include <cstring>

namespace trading {

using namespace simdjson;

namespace {

CoinbaseMsgType typeFromString(std::string_view s) noexcept {
    if (s == "subscriptions") return CoinbaseMsgType::Subscriptions;
    if (s == "snapshot") return CoinbaseMsgType::Snapshot;
    if (s == "l2update") return CoinbaseMsgType::L2Update;
    if (s == "heartbeat") return CoinbaseMsgType::Heartbeat;
    if (s == "error") return CoinbaseMsgType::Error;
    return CoinbaseMsgType::Unknown;
}

inline bool parseDouble(std::string_view s, double& out) noexcept {
    char buf[64];
    if (s.size() >= sizeof(buf)) return false;
    std::memcpy(buf, s.data(), s.size());
    buf[s.size()] = '\0';
    char* end = nullptr;
    out = std::strtod(buf, &end);
    return end != buf;
}

}  // namespace

CoinbaseParser::CoinbaseParser() {
    padded_.reserve(4096);
}

Error CoinbaseParser::parse(std::string_view jsonText, CoinbaseMessage& out) noexcept {
    out.bids.clear();
    out.asks.clear();
    out.changes.clear();
    out.product_id.clear();
    out.error_message.clear();
    out.sequence = 0;
    out.type = CoinbaseMsgType::Unknown;

    padded_.assign(jsonText);
    padded_.resize(jsonText.size() + SIMDJSON_PADDING, '\0');
    padded_.resize(jsonText.size());

    try {
        ondemand::document doc;
        auto iterErr = parser_.iterate(padded_.data(), padded_.size(), padded_.capacity()).get(doc);
        if (iterErr) return Error::ParseMalformedJson;

        ondemand::object root;
        if (doc.get_object().get(root)) return Error::ParseMalformedJson;

        std::string_view typeStr;
        if (root["type"].get_string().get(typeStr)) {
            return Error::ParseUnexpectedShape;
        }
        out.type = typeFromString(typeStr);

        std::string_view productId;
        if (!root["product_id"].get_string().get(productId)) {
            out.product_id.assign(productId);
        }

        switch (out.type) {
            case CoinbaseMsgType::Snapshot: {
                ondemand::array bids;
                if (!root["bids"].get_array().get(bids)) {
                    for (auto lvl : bids) {
                        auto arr = lvl.get_array();
                        if (arr.error()) continue;
                        CoinbasePriceLevel entry;
                        int slot = 0;
                        for (auto item : arr) {
                            std::string_view sv;
                            if (item.get_string().get(sv)) continue;
                            double v;
                            if (!parseDouble(sv, v)) continue;
                            if (slot == 0)
                                entry.price = v;
                            else if (slot == 1)
                                entry.qty = v;
                            ++slot;
                        }
                        if (slot >= 2) out.bids.push_back(entry);
                    }
                }
                ondemand::array asks;
                if (!root["asks"].get_array().get(asks)) {
                    for (auto lvl : asks) {
                        auto arr = lvl.get_array();
                        if (arr.error()) continue;
                        CoinbasePriceLevel entry;
                        int slot = 0;
                        for (auto item : arr) {
                            std::string_view sv;
                            if (item.get_string().get(sv)) continue;
                            double v;
                            if (!parseDouble(sv, v)) continue;
                            if (slot == 0)
                                entry.price = v;
                            else if (slot == 1)
                                entry.qty = v;
                            ++slot;
                        }
                        if (slot >= 2) out.asks.push_back(entry);
                    }
                }
                break;
            }
            case CoinbaseMsgType::L2Update: {
                ondemand::array changes;
                if (!root["changes"].get_array().get(changes)) {
                    for (auto ch : changes) {
                        auto arr = ch.get_array();
                        if (arr.error()) continue;
                        CoinbaseChange cg{};
                        int slot = 0;
                        for (auto item : arr) {
                            std::string_view sv;
                            if (item.get_string().get(sv)) continue;
                            if (slot == 0) {
                                cg.side = (sv == "buy") ? Side::Bid : Side::Ask;
                            } else {
                                double v;
                                if (!parseDouble(sv, v)) continue;
                                if (slot == 1)
                                    cg.price = v;
                                else if (slot == 2)
                                    cg.qty = v;
                            }
                            ++slot;
                        }
                        if (slot >= 3) out.changes.push_back(cg);
                    }
                }
                break;
            }
            case CoinbaseMsgType::Heartbeat: {
                uint64_t seq = 0;
                if (!root["sequence"].get_uint64().get(seq)) {
                    out.sequence = seq;
                }
                break;
            }
            case CoinbaseMsgType::Error: {
                std::string_view msgStr;
                if (!root["message"].get_string().get(msgStr)) {
                    out.error_message.assign(msgStr);
                }
                break;
            }
            case CoinbaseMsgType::Subscriptions:
            case CoinbaseMsgType::Unknown: break;
        }
    } catch (const simdjson_error&) {
        return Error::ParseMalformedJson;
    }
    return Error::Ok;
}

}  // namespace trading
