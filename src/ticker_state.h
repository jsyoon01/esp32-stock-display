#pragma once

#include <Arduino.h>
#include <stdint.h>

#include "app_config.h"

// 1-minute candle history (fixed-size, compact, no heap)
#define MAX_CANDLES TRADING_MINUTES

struct CandleHistory {
  int32_t closeScaled[MAX_CANDLES];
  uint8_t validBits[(MAX_CANDLES + 7) / 8];
  uint16_t count;
  int32_t minCloseScaled;
  int32_t maxCloseScaled;

  void init();
  bool isValid(uint16_t minuteIndex) const;
  void setClose(uint16_t minuteIndex, int32_t closeScaledValue);
  int16_t firstValidIndex() const;
  int16_t lastValidIndex() const;
  void updateMinMax();
};

struct TickerState {
  const char* symbol;
  CandleHistory history;
  bool hasBackfill;
  uint32_t candleDayKey; // YYYYMMDD from candle datetimes (regular session)

  // Displayed last price (prefer quote last, else candle last).
  float lastPrice;

  // Quote data (official previous_close + last).
  bool hasQuote;
  float quoteLastPrice;
  unsigned long lastQuoteFetchMs;

  // Candle-derived last price (for fallback and debugging).
  float candleLastPrice;

  float prevClose;       // previous regular session close (from candles)
  uint32_t prevCloseDayKey;
  float changePercent;

  unsigned long lastCandleFetchMs; // last successful time_series load (millis)

  // NTP-derived session basis for the currently loaded history.
  uint32_t sessionBasisDayKey;
  bool sessionBasisOpen;
};

// Compute/update derived fields from candle data.
void updateDerivedFromCandles(TickerState& ts);

// Ensure the given ticker has fresh-enough candles for the current session basis.
// - sessionOpen: true during regular session; false otherwise.
// - nowLocal: NY local time from NTP.
bool ensureCandlesFresh(TickerState& ts, const struct tm& nowLocal, bool sessionOpen);

// Ensure quote freshness for official previous_close + last price.
bool ensureQuoteFresh(TickerState& ts, const struct tm& nowLocal, bool sessionOpen);


