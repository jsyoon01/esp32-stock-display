#include "twelvedata.h"

#include <math.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>

#include "app_config.h"
#include "time_sync.h"

static unsigned long nextApiAllowedAtMs = 0;
static bool lastSpacedThrottled = false;

bool twelvedataCanRequestNow() {
  return millis() >= nextApiAllowedAtMs;
}

unsigned long twelvedataNextAllowedAtMs() {
  return nextApiAllowedAtMs;
}

static void agentLogJson(const char* hypothesisId, const char* location, const char* message,
                         const char* symbol, int httpCode, uint32_t u32a, uint32_t u32b, float f1, float f2) {
  // Writes a single-line JSON entry to Serial (user will redirect Serial output to .cursor/debug.log).
  // IMPORTANT: do not log secrets (API key, WiFi password).
  Serial.printf(
      "{\"sessionId\":\"debug-session\",\"runId\":\"run1\",\"hypothesisId\":\"%s\",\"location\":\"%s\","
      "\"message\":\"%s\",\"timestamp\":%lu,\"data\":{\"symbol\":\"%s\",\"httpCode\":%d,"
      "\"u32a\":%lu,\"u32b\":%lu,\"f1\":%.6f,\"f2\":%.6f}}\n",
      hypothesisId, location, message, (unsigned long)millis(), (symbol ? symbol : ""), httpCode,
      (unsigned long)u32a, (unsigned long)u32b, (double)f1, (double)f2);
}

static String urlEncodeDateTimeParam(const String& s) {
  String out;
  out.reserve(s.length() + 8);
  for (size_t i = 0; i < s.length(); i++) {
    char ch = s[i];
    if (ch == ' ') out += "%20";
    else if (ch == ':') out += "%3A";
    else out += ch;
  }
  return out;
}

static bool httpGetString(const String& url, String& outPayload) {
  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;

  http.begin(client, url);
  http.setTimeout(HTTP_TIMEOUT_MS);
  int httpCode = http.GET();
  if (httpCode != 200) {
    // #region agent log
    agentLogJson("H2", "twelvedata.cpp:httpGetString", "http_non_200", "", httpCode, 0, 0, 0.0f, 0.0f);
    // #endregion
    Serial.printf("HTTP %d\n", httpCode);
    if (httpCode == 429) {
      nextApiAllowedAtMs = millis() + 60000;
    }
    http.end();
    return false;
  }

  outPayload = http.getString();
  http.end();
  return true;
}

static bool httpGetStringSpaced(const String& url, String& outPayload) {
  unsigned long now = millis();
  if (now < nextApiAllowedAtMs) {
    lastSpacedThrottled = true;
    // #region agent log
    agentLogJson("H2", "twelvedata.cpp:httpGetStringSpaced", "throttled", "", 0,
                 (uint32_t)now, (uint32_t)nextApiAllowedAtMs, 0.0f, 0.0f);
    // #endregion
    return false;
  }
  lastSpacedThrottled = false;
  bool ok = httpGetString(url, outPayload);
  unsigned long after = millis() + API_MIN_SPACING_MS;
  if (nextApiAllowedAtMs < after) nextApiAllowedAtMs = after;
  return ok;
}

static bool readLineFromString(const String& s, int& pos, char* out, size_t outSize) {
  if (pos >= (int)s.length()) return false;
  size_t i = 0;
  while (pos < (int)s.length()) {
    char ch = s[pos++];
    if (ch == '\n') break;
    if (ch == '\r') continue;
    if (i < outSize - 1) out[i++] = ch;
  }
  out[i] = '\0';
  return true;
}

static uint32_t parseDayKeyFromDatetime(const char* dt) {
  // Expected: "YYYY-MM-DD HH:MM:SS"
  if (!dt) return 0;
  if (strlen(dt) < 10) return 0;
  uint16_t y = (uint16_t)((dt[0] - '0') * 1000 + (dt[1] - '0') * 100 + (dt[2] - '0') * 10 + (dt[3] - '0'));
  uint8_t m = (uint8_t)((dt[5] - '0') * 10 + (dt[6] - '0'));
  uint8_t d = (uint8_t)((dt[8] - '0') * 10 + (dt[9] - '0'));
  return (uint32_t)y * 10000u + (uint32_t)m * 100u + (uint32_t)d;
}

static bool parseHourMinuteFromDatetime(const char* dt, int& hourOut, int& minuteOut) {
  hourOut = 0;
  minuteOut = 0;
  if (!dt) return false;
  if (strlen(dt) < 16) return false;
  if (dt[10] != ' ') return false;
  int hh = (dt[11] - '0') * 10 + (dt[12] - '0');
  int mm = (dt[14] - '0') * 10 + (dt[15] - '0');
  if (hh < 0 || hh > 23 || mm < 0 || mm > 59) return false;
  hourOut = hh;
  minuteOut = mm;
  return true;
}

static uint16_t countTimeSeriesCsvDataRows(const String& payload) {
  int p = 0;
  while (p < (int)payload.length() && (payload[p] == ' ' || payload[p] == '\n' || payload[p] == '\r' || payload[p] == '\t')) p++;
  if (p < (int)payload.length() && payload[p] == '{') return 0;

  char line[256];
  int pos = 0;
  bool skippedHeader = false;
  while (readLineFromString(payload, pos, line, sizeof(line))) {
    if (line[0] == '\0') continue;
    if (!skippedHeader) {
      skippedHeader = true;
      if (strncmp(line, "datetime", 8) == 0) continue;
    }
    if (strncmp(line, "datetime", 8) == 0) continue;
    return 1;
  }
  return 0;
}

static bool parseSessionCloseFromTimeSeriesCsv(const String& payload, float& outClose) {
  outClose = 0.0f;
  int p = 0;
  while (p < (int)payload.length() && (payload[p] == ' ' || payload[p] == '\n' || payload[p] == '\r' || payload[p] == '\t')) p++;
  if (p < (int)payload.length() && payload[p] == '{') return false;

  char line[256];
  int pos = 0;
  bool skippedHeader = false;
  char delim = ',';
  uint32_t targetDayKey = 0;
  int bestMinute = -1;
  float bestClose = 0.0f;

  while (readLineFromString(payload, pos, line, sizeof(line))) {
    if (line[0] == '\0') continue;
    if (!skippedHeader) {
      skippedHeader = true;
      if (strchr(line, ';') != nullptr) delim = ';';
      if (strncmp(line, "datetime", 8) == 0) continue;
    }

    char delimStr[2] = {delim, '\0'};
    char* saveptr = nullptr;
    char* datetimeStr = strtok_r(line, delimStr, &saveptr);
    (void)strtok_r(nullptr, delimStr, &saveptr); // open
    (void)strtok_r(nullptr, delimStr, &saveptr); // high
    (void)strtok_r(nullptr, delimStr, &saveptr); // low
    char* closeStr = strtok_r(nullptr, delimStr, &saveptr);
    if (!datetimeStr || !closeStr) continue;

    uint32_t rowDayKey = parseDayKeyFromDatetime(datetimeStr);
    if (rowDayKey == 0) continue;
    if (targetDayKey == 0) targetDayKey = rowDayKey;
    if (rowDayKey != targetDayKey) continue;

    int hh = 0, mm = 0;
    if (!parseHourMinuteFromDatetime(datetimeStr, hh, mm)) continue;
    int minutesSinceOpen = (hh * 60 + mm) - MARKET_OPEN_MINUTES;
    if (minutesSinceOpen < 0 || minutesSinceOpen >= TRADING_MINUTES) continue;

    float c = (float)atof(closeStr);
    if (c <= 0.0f) continue;
    if (minutesSinceOpen > bestMinute) {
      bestMinute = minutesSinceOpen;
      bestClose = c;
    }
  }

  if (bestMinute < 0 || bestClose <= 0.0f) return false;
  outClose = bestClose;
  return true;
}

static bool parseAndLoadTimeSeriesCsv(TickerState& ts, const String& payload) {
  int p = 0;
  while (p < (int)payload.length() && (payload[p] == ' ' || payload[p] == '\n' || payload[p] == '\r' || payload[p] == '\t')) p++;
  if (p < (int)payload.length() && payload[p] == '{') {
    StaticJsonDocument<512> doc;
    DeserializationError err = deserializeJson(doc, payload);
    if (!err) {
      const char* msg = doc["message"] | doc["error"] | "unknown";
      int code = doc["code"] | 0;
      // #region agent log
      agentLogJson("H1", "twelvedata.cpp:parseAndLoadTimeSeriesCsv", "time_series_json_error",
                   ts.symbol, 200, (uint32_t)code, 0, 0.0f, 0.0f);
      // #endregion
      Serial.printf("TD time_series error (code=%d): %s\n", code, msg);
      if (code == 429) nextApiAllowedAtMs = millis() + 60000;
    } else {
      Serial.println("TD time_series: unexpected JSON/error payload");
    }
    return false;
  }

  ts.history.init();
  ts.hasBackfill = false;
  ts.candleDayKey = 0;
  uint32_t targetDayKey = 0;

  char line[256];
  int pos = 0;
  bool skippedHeader = false;
  char delim = ',';
  while (readLineFromString(payload, pos, line, sizeof(line))) {
    if (line[0] == '\0') continue;

    if (!skippedHeader) {
      skippedHeader = true;
      if (strchr(line, ';') != nullptr) delim = ';';
      if (strncmp(line, "datetime", 8) == 0) continue;
    }

    char delimStr[2] = {delim, '\0'};
    char* saveptr = nullptr;
    char* datetimeStr = strtok_r(line, delimStr, &saveptr);
    (void)strtok_r(nullptr, delimStr, &saveptr); // open
    (void)strtok_r(nullptr, delimStr, &saveptr); // high
    (void)strtok_r(nullptr, delimStr, &saveptr); // low
    char* closeStr = strtok_r(nullptr, delimStr, &saveptr);
    if (!datetimeStr || !closeStr) continue;

    uint32_t rowDayKey = parseDayKeyFromDatetime(datetimeStr);
    if (rowDayKey == 0) continue;
    if (targetDayKey == 0) {
      targetDayKey = rowDayKey;
      ts.candleDayKey = targetDayKey;
    } else if (rowDayKey != targetDayKey) {
      continue;
    }

    int hh = 0, mm = 0;
    if (!parseHourMinuteFromDatetime(datetimeStr, hh, mm)) continue;
    int minutesSinceOpen = (hh * 60 + mm) - MARKET_OPEN_MINUTES;
    if (minutesSinceOpen < 0 || minutesSinceOpen >= TRADING_MINUTES) continue;

    float c = (float)atof(closeStr);
    if (c <= 0.0f) continue;
    int32_t cScaled = (int32_t)lroundf(c * (float)PRICE_SCALE);
    ts.history.setClose((uint16_t)minutesSinceOpen, cScaled);
  }

  ts.history.updateMinMax();
  ts.hasBackfill = (ts.history.count > 0);
  if (ts.hasBackfill) {
    int16_t lastIdx = ts.history.lastValidIndex();
    if (lastIdx >= 0) {
      ts.candleLastPrice = (float)ts.history.closeScaled[(uint16_t)lastIdx] / (float)PRICE_SCALE;
    }
  } else {
    ts.candleLastPrice = 0.0f;
  }

  // #region agent log
  agentLogJson("H4", "twelvedata.cpp:parseAndLoadTimeSeriesCsv", "time_series_parsed",
               ts.symbol, 200, ts.candleDayKey, (uint32_t)ts.history.count, ts.candleLastPrice, 0.0f);
  // #endregion

  return ts.hasBackfill;
}

static bool fetchPrevCloseForDayKey(TickerState& ts, uint32_t sessionDayKey) {
  char date[11];
  formatDayKeyToDate(sessionDayKey, date);
  String start = String(date) + " 09:30:00";
  String end = String(date) + " 16:00:00";
  String startEnc = urlEncodeDateTimeParam(start);
  String endEnc = urlEncodeDateTimeParam(end);

  String url = String(TWELVEDATA_TIME_SERIES_URL) +
               "?symbol=" + ts.symbol +
               "&interval=1min" +
               "&start_date=" + startEnc +
               "&end_date=" + endEnc +
               "&timezone=America/New_York" +
               "&format=csv" +
               "&apikey=" + TWELVEDATA_API_KEY;

  String payload;
  // Do not print URL (contains API key)
  Serial.printf("TD prevclose: symbol=%s day=%lu\n", ts.symbol, (unsigned long)sessionDayKey);
  if (!httpGetStringSpaced(url, payload)) return false;
  float closeVal = 0.0f;
  if (!parseSessionCloseFromTimeSeriesCsv(payload, closeVal)) return false;
  ts.prevClose = closeVal;
  ts.prevCloseDayKey = sessionDayKey;
  return true;
}

bool fetchPrevCloseWithLookback(TickerState& ts, uint32_t startDayKey, const char* logPrefix) {
  uint32_t candidate = mostRecentWeekdayDayKey(startDayKey);
  for (uint8_t attempt = 0; attempt < (uint8_t)CLOSED_MARKET_LOOKBACK_DAYS; attempt++) {
    char date[11];
    formatDayKeyToDate(candidate, date);
    Serial.print(logPrefix);
    Serial.print(" try ");
    Serial.print((unsigned)(attempt + 1));
    Serial.print("/");
    Serial.print((unsigned)CLOSED_MARKET_LOOKBACK_DAYS);
    Serial.print(" ");
    Serial.print(ts.symbol);
    Serial.print(" ");
    Serial.println(date);

    if (fetchPrevCloseForDayKey(ts, candidate)) {
      Serial.print(logPrefix);
      Serial.print(" selected ");
      Serial.println(date);
      return true;
    }
    if (lastSpacedThrottled) {
      Serial.print(logPrefix);
      Serial.println(" throttled_stop");
      return false;
    }
    candidate = mostRecentWeekdayDayKey(addDaysToDayKey(candidate, -1));
  }
  Serial.print(logPrefix);
  Serial.println(" no_prevclose_found_in_lookback");
  return false;
}

bool fetchAndLoadRegularSessionClosedMarket(TickerState& ts, uint32_t sessionDayKey) {
  char date[11];
  formatDayKeyToDate(sessionDayKey, date);
  String start = String(date) + " 09:30:00";
  String end = String(date) + " 16:00:00";
  String startEnc = urlEncodeDateTimeParam(start);
  String endEnc = urlEncodeDateTimeParam(end);

  String url = String(TWELVEDATA_TIME_SERIES_URL) +
               "?symbol=" + ts.symbol +
               "&interval=1min" +
               "&start_date=" + startEnc +
               "&end_date=" + endEnc +
               "&timezone=America/New_York" +
               "&format=csv" +
               "&apikey=" + TWELVEDATA_API_KEY;

  Serial.printf("TD candles (closed): symbol=%s day=%lu\n", ts.symbol, (unsigned long)sessionDayKey);
  String payload;
  if (!httpGetStringSpaced(url, payload)) return false;
  if (countTimeSeriesCsvDataRows(payload) == 0) return false;
  return parseAndLoadTimeSeriesCsv(ts, payload);
}

bool fetchAndLoadRegularSessionOpenMarket(TickerState& ts, uint32_t todayDayKey) {
  char date[11];
  formatDayKeyToDate(todayDayKey, date);
  String start = String(date) + " 09:30:00";
  String startEnc = urlEncodeDateTimeParam(start);

  String url = String(TWELVEDATA_TIME_SERIES_URL) +
               "?symbol=" + ts.symbol +
               "&interval=1min" +
               "&start_date=" + startEnc +
               "&timezone=America/New_York" +
               "&format=csv" +
               "&apikey=" + TWELVEDATA_API_KEY;

  // Do not print URL (contains API key)
  Serial.printf("TD candles (open): symbol=%s day=%lu\n", ts.symbol, (unsigned long)todayDayKey);
  String payload;
  if (!httpGetStringSpaced(url, payload)) return false;
  return parseAndLoadTimeSeriesCsv(ts, payload);
}

bool loadMostRecentClosedMarketSession(TickerState& ts, uint32_t startDayKey, const char* logPrefix) {
  uint32_t candidate = mostRecentWeekdayDayKey(startDayKey);
  for (uint8_t attempt = 0; attempt < (uint8_t)CLOSED_MARKET_LOOKBACK_DAYS; attempt++) {
    char date[11];
    formatDayKeyToDate(candidate, date);
    Serial.print(logPrefix);
    Serial.print(" try ");
    Serial.print((unsigned)(attempt + 1));
    Serial.print("/");
    Serial.print((unsigned)CLOSED_MARKET_LOOKBACK_DAYS);
    Serial.print(" ");
    Serial.print(ts.symbol);
    Serial.print(" ");
    Serial.println(date);

    if (fetchAndLoadRegularSessionClosedMarket(ts, candidate)) {
      Serial.print(logPrefix);
      Serial.print(" selected ");
      char selected[11];
      formatDayKeyToDate(ts.candleDayKey != 0 ? ts.candleDayKey : candidate, selected);
      Serial.print(selected);
      Serial.print(" candles=");
      Serial.println(ts.history.count);
      return true;
    }
    if (lastSpacedThrottled) {
      Serial.print(logPrefix);
      Serial.println(" throttled_stop");
      return false;
    }
    candidate = mostRecentWeekdayDayKey(addDaysToDayKey(candidate, -1));
  }
  Serial.print(logPrefix);
  Serial.println(" no_session_found_in_lookback");
  return false;
}

bool fetchQuote(TickerState& ts) {
  String url = String(TWELVEDATA_QUOTE_URL) + "?symbol=" + ts.symbol + "&apikey=" + TWELVEDATA_API_KEY;
  // Do not print URL (contains API key)
  Serial.printf("TD quote: symbol=%s\n", ts.symbol);

  String payload;
  if (!httpGetStringSpaced(url, payload)) return false;

  StaticJsonDocument<2048> doc;
  DeserializationError err = deserializeJson(doc, payload);
  if (err) {
    Serial.println("TD quote: failed to parse JSON");
    Serial.println(err.c_str());
    return false;
  }

  const char* status = doc["status"] | "";
  if (strcmp(status, "error") == 0) {
    const char* msg = doc["message"] | doc["error"] | "unknown";
    int code = doc["code"] | 0;
    // #region agent log
    agentLogJson("H1", "twelvedata.cpp:fetchQuote", "quote_error",
                 ts.symbol, 200, (uint32_t)code, 0, 0.0f, 0.0f);
    // #endregion
    Serial.printf("TD quote error (code=%d): %s\n", code, msg);
    if (code == 429) nextApiAllowedAtMs = millis() + 60000;
    return false;
  }

  float current = doc["close"] | doc["price"] | doc["last"] | 0.0f;
  float prevClose = doc["previous_close"] | doc["prev_close"] | 0.0f;

  if (current <= 0.0f) {
    const char* curStr = doc["close"].as<const char*>();
    if (curStr) current = (float)atof(curStr);
  }
  if (prevClose <= 0.0f) {
    const char* pcStr = doc["previous_close"].as<const char*>();
    if (pcStr) prevClose = (float)atof(pcStr);
  }

  if (current <= 0.0f && prevClose <= 0.0f) return false;

  ts.quoteLastPrice = current;
  if (prevClose > 0.0f) ts.prevClose = prevClose;
  ts.hasQuote = (ts.quoteLastPrice > 0.0f);
  ts.lastQuoteFetchMs = millis();

  // #region agent log
  agentLogJson("H3", "twelvedata.cpp:fetchQuote", "quote_ok",
               ts.symbol, 200, 0, 0, ts.quoteLastPrice, ts.prevClose);
  // #endregion
  return true;
}


