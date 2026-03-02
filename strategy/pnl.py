from log import buffer_log

def mark_to_market(orderBook, symbol, position, avg_entry_price):
    if position == 0:
        return 0.0
    try:
        top_bid = orderBook.top_of_book(symbol, True)
        return position * (top_bid.price - avg_entry_price)
    except Exception as e:
        buffer_log(symbol, f"[ERROR] Mark-to-market failed: {e}")
        return 0.0


def print_account_summary(state, orderBook, symbol):
    unrealized = mark_to_market(orderBook, symbol, state.position, state.avg_entry_price)
    net_pnl = state.realized_pnl + unrealized

    try:
        top_bid_price = orderBook.top_of_book(symbol, True).price
        market_value = state.position * top_bid_price
        buffer_log(symbol, f"[SIMULATED] [{symbol}] Cash: ${state.cash:.2f} | Position: {state.position:.4f} @ {state.avg_entry_price:.2f} "
                           f"(Market Value: ${market_value:.2f})")
    except Exception:
        buffer_log(symbol, f"[SIMULATED] [{symbol}] Cash: ${state.cash:.2f} | Position: {state.position:.4f} @ {state.avg_entry_price:.2f} "
                           "(Market Value: awaiting live market data)")

    buffer_log(symbol, f"[SIMULATED] [{symbol}] Realized PnL: ${state.realized_pnl:.2f} | Unrealized PnL: ${unrealized:.2f} | Total PnL: ${net_pnl:.2f}\n")

def on_exec_report(rpt, symbol_states, symbol):
    state = symbol_states[symbol]

    if rpt.isFill:
        size = rpt.execSize
        price = rpt.execPrice
        buffer_log(symbol, f"\033[91m[EXEC] [{symbol}] {rpt.orderId} {size:.4f} @ {price:.2f} (average price) | {'BUY' if rpt.isBuy else 'SELL'}\033[0m")

        if rpt.isBuy:
            total_cost = state.avg_entry_price * state.position + price * size
            state.position += size
            state.avg_entry_price = total_cost / state.position if state.position > 0 else 0.0
            state.cash -= size * price
        else:
            state.realized_pnl += (price - state.avg_entry_price) * size
            state.position -= size
            state.cash += size * price
            if state.position <= 1e-8:
                state.avg_entry_price = 0.0

        if rpt.orderId in state.active_orders:
            del state.active_orders[rpt.orderId]
