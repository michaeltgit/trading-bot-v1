#include "trading/core/metrics.hpp"

namespace trading {

void Metrics::dump(std::ostream& os) const {
    os << "=== Latency histograms ===\n";
    wireToParse.dump(os, "  wire->parse    ");  os << "\n";
    parseToBook.dump(os, "  parse->book    ");  os << "\n";
    bookToSignal.dump(os, "  book->signal   "); os << "\n";
    signalToOrder.dump(os, "  signal->order  "); os << "\n";
    orderToFill.dump(os, "  order->fill    "); os << "\n";
    tickToSignal.dump(os, "  wire->signal   "); os << "\n";
    queueDepth.dump(os, "  queue depth    "); os << "\n";

    os << "=== Counters ===\n"
       << "  msgsReceived=" << msgsReceived.load()
       << " parseErrors=" << parseErrors.load()
       << " reconnects=" << reconnects.load()
       << " heartbeatsRecv=" << heartbeatsRecv.load() << "\n"
       << "  signalsGenerated=" << signalsGenerated.load()
       << " ordersSent=" << ordersSent.load()
       << " ordersApproved=" << ordersApproved.load()
       << " ordersRejected=" << ordersRejected.load()
       << " fills=" << fills.load()
       << " partialFills=" << partialFills.load() << "\n"
       << "  queueFullDrops=" << queueFullDrops.load() << "\n";
}

} // namespace trading
