#include "trading/market_data_connector.hpp"

#include <boost/asio/connect.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/ssl.hpp>
#include <boost/beast/websocket.hpp>
#include <boost/beast/websocket/ssl.hpp>

#include <curl/curl.h>
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <mutex>
#include <regex>
#include <string>
#include <thread>
#include <vector>

namespace trading {

using tcp = boost::asio::ip::tcp;
namespace websocket = boost::beast::websocket;
namespace ssl = boost::asio::ssl;
using json = nlohmann::json;

static size_t WriteCallback(void* contents, size_t size, size_t nmemb, void* userp) {
    ((std::string*)userp)->append((char*)contents, size * nmemb);
    return size * nmemb;
}

class MarketDataConnector::BinanceWSClient {
public:
    BinanceWSClient(MarketDataConnector& parent, const std::string& uri, const std::string& symbol)
        : parent_(parent), symbol_(symbol), uri_(uri), running_(false), active_ws_(nullptr) {}

    void start() {
        running_ = true;
        thread_ = std::thread([this]() { run(); });
    }

    void stop() {
        running_ = false;
        {
            std::lock_guard<std::mutex> lk(ws_mtx_);
            if (active_ws_) {
                boost::system::error_code ec;
                active_ws_->next_layer().shutdown(ec);
            }
        }
        if (thread_.joinable()) {
            thread_.join();
        }
    }

private:
    using WsStream = websocket::stream<ssl::stream<tcp::socket>>;

    void run() {
        std::smatch m;
        std::regex re(R"(wss://([^/:]+)(?::(\d+))?/ws/(.+))");
        if (!std::regex_match(uri_, m, re)) {
            spdlog::error("[{}] Invalid WebSocket URI: {}", symbol_, uri_);
            return;
        }

        const std::string host = m[1].str();
        const std::string port = m[2].matched ? m[2].str() : "443";
        const std::string path = "/ws/" + m[3].str();

        while (running_) {
            try {
                attemptConnection(host, port, path);
            } catch (const std::exception& e) {
                if (!running_) break;
                spdlog::warn("[{}] WebSocket error: {}. Reconnecting in 5s...", symbol_, e.what());
                std::this_thread::sleep_for(std::chrono::seconds(5));
            }
        }
    }

    void attemptConnection(const std::string& host, const std::string& port, const std::string& path) {
        std::string snapshot_url = "https://api.binance.us/api/v3/depth?symbol=" + symbol_ + "&limit=1000";
        CURL* curl = curl_easy_init();
        std::string readBuffer;

        if (curl) {
            curl_easy_setopt(curl, CURLOPT_URL, snapshot_url.c_str());
            curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
            curl_easy_setopt(curl, CURLOPT_WRITEDATA, &readBuffer);
            curl_easy_setopt(curl, CURLOPT_USERAGENT, "trading-bot/1.0");
            CURLcode res = curl_easy_perform(curl);
            curl_easy_cleanup(curl);
            if (res != CURLE_OK) {
                throw std::runtime_error("Snapshot fetch failed for " + symbol_);
            }
        }

        auto snap = json::parse(readBuffer);
        std::vector<OrderBookEntry> bids, asks;
        for (const auto& b : snap["bids"]) {
            bids.push_back({std::stod(b[0].get<std::string>()), std::stod(b[1].get<std::string>())});
        }
        for (const auto& a : snap["asks"]) {
            asks.push_back({std::stod(a[0].get<std::string>()), std::stod(a[1].get<std::string>())});
        }
        parent_.getOrderBook().resetTopLevels(bids, asks);

        boost::asio::io_context ioc;
        ssl::context ctx(ssl::context::tlsv12_client);
        ctx.set_default_verify_paths();
        tcp::resolver resolver(ioc);
        WsStream ws(ioc, ctx);

        {
            std::lock_guard<std::mutex> lk(ws_mtx_);
            active_ws_ = &ws;
        }

        // Ensure active_ws_ is cleared even if we throw.
        struct WsGuard {
            BinanceWSClient& self;
            ~WsGuard() {
                std::lock_guard<std::mutex> lk(self.ws_mtx_);
                self.active_ws_ = nullptr;
            }
        } guard{*this};

        auto results = resolver.resolve(host, port);
        boost::asio::connect(ws.next_layer().next_layer(), results.begin(), results.end());
        ws.next_layer().handshake(ssl::stream_base::client);

        ws.set_option(websocket::stream_base::decorator([&host](websocket::request_type& req) {
            req.set(boost::beast::http::field::host, host);
            req.set(boost::beast::http::field::user_agent, "trading-bot/1.0");
        }));

        ws.handshake(host, path);
        spdlog::info("[{}] WebSocket connected.", symbol_);

        while (running_) {
            boost::beast::multi_buffer buffer;
            ws.read(buffer);
            auto text = boost::beast::buffers_to_string(buffer.data());
            auto j = json::parse(text);

            if (j.contains("b")) {
                for (const auto& b : j["b"]) {
                    double price = std::stod(b[0].get<std::string>());
                    double size  = std::stod(b[1].get<std::string>());
                    parent_.getOrderBook().onUpdate(Side::Bid, price, size);
                    parent_.emit({symbol_, price, size, true, "BinanceUS"});
                }
            }

            if (j.contains("a")) {
                for (const auto& a : j["a"]) {
                    double price = std::stod(a[0].get<std::string>());
                    double size  = std::stod(a[1].get<std::string>());
                    parent_.getOrderBook().onUpdate(Side::Ask, price, size);
                    parent_.emit({symbol_, price, size, false, "BinanceUS"});
                }
            }
        }
    }

    MarketDataConnector& parent_;
    std::string symbol_;
    std::string uri_;
    std::thread thread_;
    std::atomic<bool> running_;

    std::mutex ws_mtx_;
    WsStream* active_ws_;
};

MarketDataConnector::MarketDataConnector(std::string symbol)
    : symbol_(std::move(symbol)), orderBook_(symbol_) {}

MarketDataConnector::~MarketDataConnector() {
    disconnect();
}

void MarketDataConnector::connect() {
    if (!wsClient_) {
        std::string streamSymbol = symbol_;
        std::transform(streamSymbol.begin(), streamSymbol.end(), streamSymbol.begin(), ::tolower);
        std::string uri = "wss://stream.binance.us:9443/ws/" + streamSymbol + "@depth@100ms";
        wsClient_ = std::make_shared<BinanceWSClient>(*this, uri, symbol_);
        wsClient_->start();
    }
}

void MarketDataConnector::disconnect() {
    if (wsClient_) {
        wsClient_->stop();
        wsClient_.reset();
    }
}

void MarketDataConnector::subscribe(const std::string&) {}

void MarketDataConnector::poll() {}

void MarketDataConnector::emit(const MarketDataUpdate& u) {
    if (cb_) {
        cb_(u);
    }
}

void MarketDataConnector::setCallback(Callback cb) {
    cb_ = std::move(cb);
}

LimitOrderBook& MarketDataConnector::getOrderBook() {
    return orderBook_;
}

const std::string& MarketDataConnector::getSymbol() const {
    return symbol_;
}

} // namespace trading
