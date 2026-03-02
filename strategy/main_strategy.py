import time
import threading
import trading_core as tc

from config import load_config
from print_utils import print_market_summary, print_order_book
from pnl import print_account_summary, on_exec_report
from order_calculator import compute_buy_order, compute_sell_order
from log import log, start_logger, stop_logger

cfg, symbols, max_pos = load_config()
logger_thread = start_logger()

class SymbolState:
    def __init__(self):
        self.cash = 10000.0
        self.position = 0.0
        self.avg_entry_price = 0.0
        self.realized_pnl = 0.0
        self.active_orders = {}
        self.last_buy_time = 0
        self.last_sell_time = 0
        self.rolling_imbalance = []
        self.prev_top_bids = []
        self.prev_top_asks = []

symbol_states = {sym: SymbolState() for sym in symbols}
workers = []
startup_threads = []

def book_changed(new, old):
    if len(new) != len(old):
        return True
    for i in range(len(new)):
        if abs(new[i].price - old[i].price) > 1e-8 or abs(new[i].size - old[i].size) > 1e-8:
            return True
    return False

def make_exec_callback(symbol):
    def callback(rpt):
        on_exec_report(rpt, symbol_states, symbol)
    return callback

def process_symbol(symbol, worker, state):
    lob = worker.get_order_book()
    bids = lob.depth(tc.Side.Bid, 6)
    asks = lob.depth(tc.Side.Ask, 6)

    if not bids or not asks:
        return

    if not book_changed(bids, state.prev_top_bids) and not book_changed(asks, state.prev_top_asks):
        return

    state.prev_top_bids = [tc.OrderBookEntry(b.price, b.size) for b in bids]
    state.prev_top_asks = [tc.OrderBookEntry(a.price, a.size) for a in asks]

    bid_total = sum(b.price * b.size for b in bids)
    ask_total = sum(a.price * a.size for a in asks)
    imbalance = bid_total / (bid_total + ask_total)
    imbalance_percent = imbalance * 100

    top_bid = bids[0]
    top_ask = asks[0]
    spread = top_ask.price - top_bid.price
    mid_price = (top_bid.price + top_ask.price) / 2
    spread_pct = spread / mid_price

    state.rolling_imbalance.append(imbalance_percent)
    if len(state.rolling_imbalance) > 5:
        state.rolling_imbalance.pop(0)

    imbalance_trending_up = (
        len(state.rolling_imbalance) > 1 and
        state.rolling_imbalance[-1] > state.rolling_imbalance[-2]
    )

    print_market_summary(symbol, spread, bid_total, ask_total, imbalance_percent)

    now = time.time()

    if (
        state.cash > 0.0 and imbalance_percent > 75 and imbalance_trending_up
        and spread_pct < 0.0003 and now - state.last_buy_time > 20
    ):
        buy_price, size = compute_buy_order(lob, symbol, state.cash, imbalance_percent, state.rolling_imbalance)
        if size > 0 and buy_price is not None:
            oid = f"BUY-{symbol}-{int(now)}"
            order = tc.NewOrder(oid, symbol, True, buy_price, size)
            worker.send_order(order)
            state.active_orders[oid] = now
            state.last_buy_time = now

    if (
        state.position > 0.0 and imbalance_percent < 25 and not imbalance_trending_up
        and spread_pct < 0.0003 and now - state.last_sell_time > 10
    ):
        sell_price, size = compute_sell_order(lob, symbol, state.position, imbalance_percent, state.rolling_imbalance)
        if size > 0 and sell_price is not None:
            oid = f"SELL-{symbol}-{int(now)}"
            order = tc.NewOrder(oid, symbol, False, sell_price, size)
            worker.send_order(order)
            state.active_orders[oid] = now
            state.last_sell_time = now

    print_account_summary(state, lob, symbol)
    print_order_book(lob, tc, symbol)

def make_data_callback(symbol, worker, state):
    def callback():
        process_symbol(symbol, worker, state)
    return callback

def setup_worker(symbol):
    worker = tc.SymbolWorker(symbol, max_pos)
    worker.set_callback(make_exec_callback(symbol))
    worker.set_data_callback(make_data_callback(symbol, worker, symbol_states[symbol]))
    worker.start()
    workers.append(worker)

for symbol in symbols:
    t = threading.Thread(target=setup_worker, args=(symbol,))
    t.start()
    startup_threads.append(t)

for t in startup_threads:
    t.join()

try:
    while True:
        time.sleep(1) # Keeps the main thread alive, all real work is happening in background threads
except KeyboardInterrupt:
    log("Interrupted")
finally:
    for w in workers:
        w.stop()
    stop_logger()
    log("Strategy terminated.")
