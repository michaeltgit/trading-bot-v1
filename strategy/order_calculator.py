import trading_core as tc

def compute_buy_order(order_book, symbol, cash_usd, imbalance_percent, imbalance_history, max_fraction=0.10, max_levels=10):
    asks = order_book.depth(tc.Side.Ask, max_levels)
    if not asks:
        return None, 0.0

    top_bid = order_book.top_of_book(symbol, True)
    top_ask = asks[0]
    spread = top_ask.price - top_bid.price

    if len(imbalance_history) == 5 and imbalance_history[-1] < imbalance_history[0]:
        return None, 0.0

    mid_price = (top_ask.price + top_bid.price) / 2
    spread_penalty = min(spread / mid_price, 0.005)
    raw_advantage = (imbalance_percent - 50.0) / 50.0
    confidence = max(0.0, min(raw_advantage * (1.0 - spread_penalty / 0.01), 1.0))

    target_dollars = cash_usd * max_fraction * confidence
    if target_dollars <= 0.0:
        return None, 0.0

    accumulated_cost = 0.0
    accumulated_size = 0.0
    limit_price = 0.0

    for lvl in asks:
        fill = min((target_dollars - accumulated_cost) / lvl.price, lvl.size)
        if fill <= 0.0:
            break
        accumulated_size += fill
        accumulated_cost += fill * lvl.price
        limit_price = lvl.price
        if accumulated_cost >= target_dollars:
            break

    return round(limit_price, 2), round(accumulated_size, 4)


def compute_sell_order(order_book, symbol, position, imbalance_percent, imbalance_history, max_fraction=1.0, max_levels=10):
    bids = order_book.depth(tc.Side.Bid, max_levels)
    if not bids:
        return None, 0.0

    top_bid = bids[0]
    top_ask = order_book.top_of_book(symbol, False)
    spread = top_ask.price - top_bid.price

    if len(imbalance_history) == 5 and imbalance_history[-1] > imbalance_history[0]:
        return None, 0.0

    mid_price = (top_ask.price + top_bid.price) / 2
    spread_penalty = min(spread / mid_price, 0.005)
    raw_advantage = (50.0 - imbalance_percent) / 40.0
    confidence = max(0.0, min(raw_advantage * (1.0 - spread_penalty / 0.01), 1.0))

    target = position * max_fraction * confidence
    if target <= 0.0:
        return None, 0.0

    accumulated = 0.0
    limit_price = 0.0

    for lvl in bids:
        fill = min(target - accumulated, lvl.size)
        if fill <= 0.0:
            break
        accumulated += fill
        limit_price = lvl.price
        if accumulated >= target:
            break

    return round(limit_price, 2), round(accumulated, 4)
