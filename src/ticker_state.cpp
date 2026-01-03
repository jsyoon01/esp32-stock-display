#include "ticker_state.h"

#include <math.h>
#include <WiFi.h>

#include "time_sync.h"
#include "twelvedata.h"

void CandleHistory::init() {
  count = 0;
  minCloseScaled = INT32_MAX;
  maxCloseScaled = 0;
  for (uint16_t i = 0; i < MAX_CANDLES; i++) {
    closeScaled[i] = 0;
  }
  for (uint16_t i = 0; i < (uint16_t)sizeof(validBits); i++) validBits[i] = 0;
}

bool CandleHistory::isValid(uint16_t minuteIndex) const {
  if (minuteIndex >= MAX_CANDLES) return false;
  uint16_t byteIdx = minuteIndex / 8;
  uint8_t bit = (uint8_t)(1u << (minuteIndex % 8));
  return (validBits[byteIdx] & bit) != 0;
}

void CandleHistory::setClose(uint16_t minuteIndex, int32_t closeScaledValue) {
  if (minuteIndex >= MAX_CANDLES) return;
  if (!isValid(minuteIndex)) {
    count++;
    uint16_t byteIdx = minuteIndex / 8;
    uint8_t bit = (uint8_t)(1u << (minuteIndex % 8));
    validBits[byteIdx] |= bit;
  }
  closeScaled[minuteIndex] = closeScaledValue;
}

int16_t CandleHistory::firstValidIndex() const {
  for (uint16_t i = 0; i < MAX_CANDLES; i++) {
    if (isValid(i)) return (int16_t)i;
  }
  return -1;
}

int16_t CandleHistory::lastValidIndex() const {
  for (int i = (int)MAX_CANDLES - 1; i >= 0; i--) {
    if (isValid((uint16_t)i)) return (int16_t)i;
  }
  return -1;
}

void CandleHistory::updateMinMax() {
  if (count == 0) {
    minCloseScaled = INT32_MAX;
    maxCloseScaled = 0;
    return;
  }

  minCloseScaled = INT32_MAX;
  maxCloseScaled = 0;

  for (uint16_t i = 0; i < MAX_CANDLES; i++) {
    if (!isValid(i)) continue;
    int32_t c = closeScaled[i];
    if (c < minCloseScaled) minCloseScaled = c;
    if (c > maxCloseScaled) maxCloseScaled = c;
  }

  int32_t range = maxCloseScaled - minCloseScaled;
  if (range < (int32_t)(PRICE_SCALE / 100)) { // < $0.01
    int32_t avg = (maxCloseScaled + minCloseScaled) / 2;
    minCloseScaled = (int32_t)lroundf((float)avg * 0.995f);
    maxCloseScaled = (int32_t)lroundf((float)avg * 1.005f);
  }
}

static void maybeUpdateChangePercent(TickerState& ts) {
  if (ts.lastPrice <= 0.0f) return;

  if (ts.prevClose > 0.0f) {
    ts.changePercent = (ts.lastPrice - ts.prevClose) / ts.prevClose * 100.0f;
    // #region agent log
    if (ts.symbol) {
      Serial.printf("{\"sessionId\":\"debug-session\",\"runId\":\"run1\",\"hypothesisId\":\"H5\","
                    "\"location\":\"ticker_state.cpp:maybeUpdateChangePercent\",\"message\":\"pct_from_prevClose\","
                    "\"timestamp\":%lu,\"data\":{\"symbol\":\"%s\",\"last\":%.6f,\"prevClose\":%.6f,\"pct\":%.6f}}\n",
                    (unsigned long)millis(), ts.symbol, (double)ts.lastPrice, (double)ts.prevClose, (double)ts.changePercent);
    }
    // #endregion
    return;
  }

  int16_t firstIdx = ts.history.firstValidIndex();
  if (firstIdx >= 0) {
    float firstClose = (float)ts.history.closeScaled[(uint16_t)firstIdx] / (float)PRICE_SCALE;
    if (firstClose > 0.0f) ts.changePercent = (ts.lastPrice - firstClose) / firstClose * 100.0f;
    // #region agent log
    if (ts.symbol) {
      Serial.printf("{\"sessionId\":\"debug-session\",\"runId\":\"run1\",\"hypothesisId\":\"H5\","
                    "\"location\":\"ticker_state.cpp:maybeUpdateChangePercent\",\"message\":\"pct_from_firstCandle\","
                    "\"timestamp\":%lu,\"data\":{\"symbol\":\"%s\",\"last\":%.6f,\"first\":%.6f,\"pct\":%.6f,\"hasBackfill\":%d}}\n",
                    (unsigned long)millis(), ts.symbol, (double)ts.lastPrice, (double)firstClose, (double)ts.changePercent, (int)ts.hasBackfill);
    }
    // #endregion
  }
}

void updateDerivedFromCandles(TickerState& ts) {
  maybeUpdateChangePercent(ts);
}

static void updateDisplayedPrice(TickerState& ts) {
  if (ts.hasQuote && ts.quoteLastPrice > 0.0f) ts.lastPrice = ts.quoteLastPrice;
  else if (ts.hasBackfill && ts.candleLastPrice > 0.0f) ts.lastPrice = ts.candleLastPrice;
  else ts.lastPrice = 0.0f;
}

bool ensureQuoteFresh(TickerState& ts, const struct tm& nowLocal, bool sessionOpen) {
  unsigned long nowMs = millis();
  if (WiFi.status() != WL_CONNECTED) return false;

  // Don't spam attempts while we're inside the global spacing window.
  if (!twelvedataCanRequestNow()) {
    // #region agent log
    if (ts.symbol && strcmp(ts.symbol, "AAPL") == 0) {
      Serial.printf("{\"sessionId\":\"debug-session\",\"runId\":\"run1\",\"hypothesisId\":\"H2\","
                    "\"location\":\"ticker_state.cpp:ensureQuoteFresh\",\"message\":\"skip_throttled\","
                    "\"timestamp\":%lu,\"data\":{\"nowMs\":%lu,\"nextAllowed\":%lu}}\n",
                    (unsigned long)millis(), (unsigned long)nowMs, (unsigned long)twelvedataNextAllowedAtMs());
    }
    // #endregion
    return false;
  }

  // #region agent log
  if (ts.symbol && strcmp(ts.symbol, "AAPL") == 0) {
    Serial.printf("{\"sessionId\":\"debug-session\",\"runId\":\"run1\",\"hypothesisId\":\"H2\","
                  "\"location\":\"ticker_state.cpp:ensureQuoteFresh\",\"message\":\"enter\","
                  "\"timestamp\":%lu,\"data\":{\"hasQuote\":%d,\"ageMs\":%lu,\"sessionOpen\":%d}}\n",
                  (unsigned long)millis(), (int)ts.hasQuote,
                  (unsigned long)(ts.hasQuote ? (nowMs - ts.lastQuoteFetchMs) : 0), (int)sessionOpen);
  }
  // #endregion

  if (!ts.hasQuote) {
    if (!fetchQuote(ts)) return false;
    updateDisplayedPrice(ts);
    updateDerivedFromCandles(ts);
    // #region agent log
    if (ts.symbol) {
      Serial.printf("{\"sessionId\":\"debug-session\",\"runId\":\"run1\",\"hypothesisId\":\"H5\","
                    "\"location\":\"ticker_state.cpp:ensureQuoteFresh\",\"message\":\"after_quote\","
                    "\"timestamp\":%lu,\"data\":{\"symbol\":\"%s\",\"hasQuote\":%d,\"quoteLast\":%.6f,\"prevClose\":%.6f,\"last\":%.6f,\"pct\":%.6f}}\n",
                    (unsigned long)millis(), ts.symbol, (int)ts.hasQuote, (double)ts.quoteLastPrice,
                    (double)ts.prevClose, (double)ts.lastPrice, (double)ts.changePercent);
    }
    // #endregion
    return true;
  }

  if (sessionOpen && (nowMs - ts.lastQuoteFetchMs) > QUOTE_FRESHNESS_MS) {
    if (!fetchQuote(ts)) return false;
    updateDisplayedPrice(ts);
    updateDerivedFromCandles(ts);
    // #region agent log
    if (ts.symbol) {
      Serial.printf("{\"sessionId\":\"debug-session\",\"runId\":\"run1\",\"hypothesisId\":\"H5\","
                    "\"location\":\"ticker_state.cpp:ensureQuoteFresh\",\"message\":\"after_quote_refresh\","
                    "\"timestamp\":%lu,\"data\":{\"symbol\":\"%s\",\"hasQuote\":%d,\"quoteLast\":%.6f,\"prevClose\":%.6f,\"last\":%.6f,\"pct\":%.6f}}\n",
                    (unsigned long)millis(), ts.symbol, (int)ts.hasQuote, (double)ts.quoteLastPrice,
                    (double)ts.prevClose, (double)ts.lastPrice, (double)ts.changePercent);
    }
    // #endregion
    return true;
  }

  return true;
}

bool ensureCandlesFresh(TickerState& ts, const struct tm& nowLocal, bool sessionOpen) {
  unsigned long nowMs = millis();
  if (WiFi.status() != WL_CONNECTED) return false;

  uint32_t basisDayKey = sessionOpen ? dayKeyFromTm(nowLocal) : closedSessionCandidateDayKeyFromTm(nowLocal);

  bool basisMatches = (ts.sessionBasisDayKey == basisDayKey) && (ts.sessionBasisOpen == sessionOpen);
  bool needsFetch = false;

  if (!ts.hasBackfill) {
    needsFetch = true;
  } else if (!basisMatches) {
    needsFetch = true;
  } else if (sessionOpen && (nowMs - ts.lastCandleFetchMs) > CANDLE_FRESHNESS_MS) {
    needsFetch = true;
  }

  if (!needsFetch) return true;

  // Don't spam attempts while we're inside the global spacing window.
  if (!twelvedataCanRequestNow()) {
    // #region agent log
    if (ts.symbol && strcmp(ts.symbol, "AAPL") == 0) {
      Serial.printf("{\"sessionId\":\"debug-session\",\"runId\":\"run1\",\"hypothesisId\":\"H2\","
                    "\"location\":\"ticker_state.cpp:ensureCandlesFresh\",\"message\":\"skip_throttled\","
                    "\"timestamp\":%lu,\"data\":{\"nowMs\":%lu,\"nextAllowed\":%lu}}\n",
                    (unsigned long)millis(), (unsigned long)nowMs, (unsigned long)twelvedataNextAllowedAtMs());
    }
    // #endregion
    return false;
  }

  // #region agent log
  if (ts.symbol && strcmp(ts.symbol, "AAPL") == 0) {
    Serial.printf("{\"sessionId\":\"debug-session\",\"runId\":\"run1\",\"hypothesisId\":\"H4\","
                  "\"location\":\"ticker_state.cpp:ensureCandlesFresh\",\"message\":\"needs_fetch\","
                  "\"timestamp\":%lu,\"data\":{\"basisDayKey\":%lu,\"basisMatches\":%d,\"sessionOpen\":%d,"
                  "\"lastCandleFetchAgeMs\":%lu,\"hasBackfill\":%d}}\n",
                  (unsigned long)millis(), (unsigned long)basisDayKey, (int)basisMatches, (int)sessionOpen,
                  (unsigned long)(ts.lastCandleFetchMs ? (nowMs - ts.lastCandleFetchMs) : 0), (int)ts.hasBackfill);
  }
  // #endregion

  bool ok = false;
  if (sessionOpen) {
    ok = fetchAndLoadRegularSessionOpenMarket(ts, basisDayKey);
  } else {
    ok = loadMostRecentClosedMarketSession(ts, basisDayKey, "TD candles (closed-probe)");
  }
  if (!ok) return false;

  ts.lastCandleFetchMs = millis();
  ts.sessionBasisDayKey = basisDayKey;
  ts.sessionBasisOpen = sessionOpen;

  // Ensure we have the previous regular session close for % change.
  // NOTE: In the new budgeted scheduler, `/quote` provides `previous_close`, so avoid extra
  // `/time_series` prev-close lookups unless we truly don't have prevClose yet.
  if (ts.candleDayKey != 0 && ts.prevClose <= 0.0f) {
    uint32_t prevSessionCandidate = previousWeekdayDayKey(ts.candleDayKey);
    if (ts.prevCloseDayKey != prevSessionCandidate || ts.prevClose <= 0.0f) {
      if (twelvedataCanRequestNow()) {
        (void)fetchPrevCloseWithLookback(ts, prevSessionCandidate, "TD prevclose");
      }
    }
  }

  updateDisplayedPrice(ts);
  updateDerivedFromCandles(ts);

  // #region agent log
  if (ts.symbol && strcmp(ts.symbol, "AAPL") == 0) {
    Serial.printf("{\"sessionId\":\"debug-session\",\"runId\":\"run1\",\"hypothesisId\":\"H4\","
                  "\"location\":\"ticker_state.cpp:ensureCandlesFresh\",\"message\":\"fetched_ok\","
                  "\"timestamp\":%lu,\"data\":{\"candleDayKey\":%lu,\"count\":%u,\"candleLast\":%.6f}}\n",
                  (unsigned long)millis(), (unsigned long)ts.candleDayKey, (unsigned)ts.history.count,
                  (double)ts.candleLastPrice);
  }
  // #endregion

  return true;
}


