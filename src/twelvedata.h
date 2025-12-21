#pragma once

#include <Arduino.h>
#include "ticker_state.h"

// Fetch + load candle history for regular session.
bool fetchAndLoadRegularSessionOpenMarket(TickerState& ts, uint32_t todayDayKey);
bool fetchAndLoadRegularSessionClosedMarket(TickerState& ts, uint32_t sessionDayKey);

// Fetch quote (official previous_close + last price).
bool fetchQuote(TickerState& ts);

// Throttle helpers (shared across quote + time_series).
bool twelvedataCanRequestNow();
unsigned long twelvedataNextAllowedAtMs();

// Closed market / holidays: probe backward until candles exist.
bool loadMostRecentClosedMarketSession(TickerState& ts, uint32_t startDayKey, const char* logPrefix);

// Previous session close lookup (bounded lookback).
bool fetchPrevCloseWithLookback(TickerState& ts, uint32_t startDayKey, const char* logPrefix);


