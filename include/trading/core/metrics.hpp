#pragma once

#include "trading/util/hdr_histogram.hpp"

#include <atomic>
#include <cstdint>
#include <ostream>

namespace trading {

struct Metrics {
    HdrHistogram wireToParse;
    HdrHistogram parseToBook;
    HdrHistogram bookToSignal;
    HdrHistogram signalToOrder;
    HdrHistogram orderToFill;
    HdrHistogram tickToSignal;
    HdrHistogram queueDepth;

    std::atomic<uint64_t> msgsReceived{0};
    std::atomic<uint64_t> parseErrors{0};
    std::atomic<uint64_t> reconnects{0};
    std::atomic<uint64_t> heartbeatsRecv{0};

    std::atomic<uint64_t> signalsGenerated{0};
    std::atomic<uint64_t> ordersSent{0};
    std::atomic<uint64_t> ordersApproved{0};
    std::atomic<uint64_t> ordersRejected{0};
    std::atomic<uint64_t> fills{0};
    std::atomic<uint64_t> partialFills{0};
    std::atomic<uint64_t> circuitBlocked{0};

    std::atomic<uint64_t> queueFullDrops{0};
    std::atomic<uint64_t> preSnapshotDrops{0};

    void dump(std::ostream& os) const;
};

}  // namespace trading
