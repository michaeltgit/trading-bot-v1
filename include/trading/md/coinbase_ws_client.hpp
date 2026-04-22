#pragma once

#include "trading/core/types.hpp"
#include "trading/md/coinbase_auth.hpp"
#include "trading/md/coinbase_messages.hpp"
#include "trading/md/coinbase_parser.hpp"

#include <atomic>
#include <chrono>
#include <functional>
#include <memory>
#include <string>
#include <thread>
#include <vector>

namespace boost::asio {
class io_context;
}

namespace trading {

struct CoinbaseWSConfig {
    std::string host = "ws-feed.exchange.coinbase.com";
    std::string port = "443";
    std::string channel = "level2_batch";
    std::vector<std::string> product_ids;
    std::chrono::seconds heartbeatTimeout{30};
    std::chrono::milliseconds backoffInitial{1000};
    std::chrono::milliseconds backoffMax{30000};
};

class CoinbaseWSClient {
public:
    using MessageCallback = std::function<void(const CoinbaseMessage&, Timestamp recvTs)>;
    using ReconnectCallback = std::function<void()>;

    CoinbaseWSClient(CoinbaseWSConfig cfg, CoinbaseAuth auth) noexcept;
    ~CoinbaseWSClient();

    CoinbaseWSClient(const CoinbaseWSClient&) = delete;
    CoinbaseWSClient& operator=(const CoinbaseWSClient&) = delete;

    void setMessageCallback(MessageCallback cb) noexcept { onMessage_ = std::move(cb); }
    void setReconnectCallback(ReconnectCallback cb) noexcept { onReconnect_ = std::move(cb); }

    void start();
    void stop();

    uint64_t messagesReceived() const noexcept { return msgsRecv_.load(std::memory_order_relaxed); }
    uint64_t parseErrors() const noexcept { return parseErrs_.load(std::memory_order_relaxed); }
    uint64_t reconnects() const noexcept { return reconnects_.load(std::memory_order_relaxed); }
    uint64_t deadManReconnects() const noexcept { return deadManReconnects_.load(std::memory_order_relaxed); }

private:
    void run() noexcept;

    CoinbaseWSConfig cfg_;
    CoinbaseAuth auth_;
    CoinbaseParser parser_;

    MessageCallback onMessage_;
    ReconnectCallback onReconnect_;

    std::atomic<bool> running_{false};
    std::atomic<bool> hasConnectedOnce_{false};
    std::thread thread_;

    std::atomic<uint64_t> msgsRecv_{0};
    std::atomic<uint64_t> parseErrs_{0};
    std::atomic<uint64_t> reconnects_{0};
    std::atomic<uint64_t> deadManReconnects_{0};
};

} // namespace trading
