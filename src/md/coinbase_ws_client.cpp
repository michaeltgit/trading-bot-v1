#include "trading/md/coinbase_ws_client.hpp"

#include "trading/core/clock.hpp"
#include "trading/core/logger.hpp"

#include <boost/asio/connect.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/ssl/host_name_verification.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/ssl.hpp>
#include <boost/beast/websocket.hpp>
#include <boost/beast/websocket/ssl.hpp>
#include <nlohmann/json.hpp>
#include <openssl/ssl.h>

#include <sys/socket.h>
#include <sys/time.h>

#include <chrono>
#include <sstream>
#include <string>

namespace trading {

using tcp = boost::asio::ip::tcp;
namespace asio = boost::asio;
namespace ssl = boost::asio::ssl;
namespace websocket = boost::beast::websocket;
using json = nlohmann::json;

namespace {

std::string buildSubscribeMessage(const CoinbaseWSConfig& cfg, const CoinbaseAuth& auth) {
    json body;
    body["type"] = "subscribe";
    body["product_ids"] = cfg.product_ids;
    body["channels"] = json::array({cfg.channel, "heartbeat"});

    if (auth.hasCredentials()) {
        auto ts = std::to_string(std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch()).count());
        body["signature"] = auth.sign(ts);
        body["key"] = auth.key();
        body["passphrase"] = auth.passphrase();
        body["timestamp"] = ts;
    }
    return body.dump();
}

} // namespace

CoinbaseWSClient::CoinbaseWSClient(CoinbaseWSConfig cfg, CoinbaseAuth auth) noexcept
    : cfg_(std::move(cfg)), auth_(std::move(auth)) {}

CoinbaseWSClient::~CoinbaseWSClient() {
    stop();
}

void CoinbaseWSClient::start() {
    if (running_.exchange(true)) return;
    thread_ = std::thread([this] { run(); });
}

void CoinbaseWSClient::stop() {
    if (!running_.exchange(false)) return;
    if (thread_.joinable()) thread_.join();
}

void CoinbaseWSClient::run() noexcept {
    using WsStream = websocket::stream<ssl::stream<tcp::socket>>;

    auto backoff = cfg_.backoffInitial;

    while (running_.load(std::memory_order_acquire)) {
        try {
            asio::io_context ioc;
            ssl::context ctx(ssl::context::tlsv12_client);
            ctx.set_default_verify_paths();
            ctx.set_verify_mode(ssl::verify_peer);
            tcp::resolver resolver(ioc);
            WsStream ws(ioc, ctx);

            auto results = resolver.resolve(cfg_.host, cfg_.port);
            asio::connect(ws.next_layer().next_layer(), results.begin(), results.end());

            // Coinbase rejects handshakes without SNI.
            if (!SSL_set_tlsext_host_name(ws.next_layer().native_handle(),
                                          cfg_.host.c_str())) {
                throw std::runtime_error("SSL_set_tlsext_host_name failed");
            }
            ws.next_layer().set_verify_callback(ssl::host_name_verification(cfg_.host));
            ws.next_layer().handshake(ssl::stream_base::client);

            ws.set_option(websocket::stream_base::decorator(
                [this](websocket::request_type& req) {
                    req.set(boost::beast::http::field::host, cfg_.host);
                    req.set(boost::beast::http::field::user_agent, "trading-engine/1.0");
                }));
            ws.handshake(cfg_.host, "/");

            ws.write(asio::buffer(buildSubscribeMessage(cfg_, auth_)));

            TRADING_LOG_INFO("coinbase-ws connected and subscribed");

            if (hasConnectedOnce_.exchange(true)) {
                ++reconnects_;
                if (onReconnect_) onReconnect_();
            }

            backoff = cfg_.backoffInitial;

            // Bound blocking reads so stop() and the dead-man timer stay responsive.
            timeval tv{};
            tv.tv_sec = 1;
            setsockopt(ws.next_layer().next_layer().native_handle(),
                       SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

            auto lastSeen = std::chrono::steady_clock::now();
            boost::beast::flat_buffer buffer;
            CoinbaseMessage msg;

            while (running_.load(std::memory_order_acquire)) {
                auto now = std::chrono::steady_clock::now();
                if (now - lastSeen > cfg_.heartbeatTimeout) {
                    TRADING_LOG_WARN("coinbase-ws heartbeat timeout, forcing reconnect");
                    ++deadManReconnects_;
                    break;
                }

                buffer.clear();
                boost::beast::error_code ec;
                ws.read(buffer, ec);
                if (ec) {
                    if (ec == boost::asio::error::would_block ||
                        ec == boost::asio::error::try_again ||
                        ec == boost::beast::error::timeout) {
                        continue;
                    }
                    throw boost::system::system_error(ec);
                }
                auto recvTs = Clock::now();
                ++msgsRecv_;

                auto text = boost::beast::buffers_to_string(buffer.data());
                Error err = parser_.parse(text, msg);
                if (err != Error::Ok) {
                    ++parseErrs_;
                    continue;
                }

                lastSeen = std::chrono::steady_clock::now();

                if (msg.type == CoinbaseMsgType::Error) {
                    TRADING_LOG_ERROR("coinbase-ws error message from server");
                }

                if (onMessage_) onMessage_(msg, recvTs);
            }
        } catch (const std::exception& e) {
            if (!running_.load(std::memory_order_acquire)) break;
            TRADING_LOG_WARN("coinbase-ws disconnect, backing off");
            std::this_thread::sleep_for(backoff);
            backoff = std::min(backoff * 2, cfg_.backoffMax);
        }
    }
}

} // namespace trading
