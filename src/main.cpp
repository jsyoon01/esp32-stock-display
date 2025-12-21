#include <Arduino.h>
#include "ESP32-HUB75-MatrixPanel-I2S-DMA.h"
#include <WiFi.h>
#include <ESPmDNS.h>

#include "app_config.h"
#include "display_ui.h"
#include "ticker_state.h"
#include "time_sync.h"
#include "twelvedata.h"
#include "ticker_manager.h"
#include "ticker_persistence.h"
#include "web_config.h"
#include "mdns_name.h"

static MatrixPanel_I2S_DMA* dma_display = nullptr;

static TickerState tickers[MAX_TICKERS];
static TickerManager tickerManager;
static uint8_t activeTickerIdx = 0;
static unsigned long activeSinceMs = 0;
static uint8_t prefetchTickerIdx = 0;

static uint8_t nextTickerIndex(uint8_t idx) {
  uint8_t n = tickerManager.count();
  if (n == 0) return 0;
  return (uint8_t)((idx + 1) % n);
}

#define my_R1_PIN 25
#define my_G1_PIN 27 // (26) falsely wired
#define my_B1_PIN 26 // (27) falsely wired
#define my_R2_PIN 14
#define my_G2_PIN 13 // (12) falsely wired
#define my_B2_PIN 12 // (13) falsely wired
#define my_A_PIN  23
#define my_B_PIN  22
#define my_C_PIN  5
#define my_D_PIN  17
#define my_E_PIN  32
#define my_LAT_PIN 4
#define my_OE_PIN  15
#define my_CLK_PIN 16

static HUB75_I2S_CFG::i2s_pins _pins = {
  my_R1_PIN,
  my_G1_PIN,
  my_B1_PIN,
  my_R2_PIN,
  my_G2_PIN,
  my_B2_PIN,
  my_A_PIN,
  my_B_PIN,
  my_C_PIN,
  my_D_PIN,
  my_E_PIN,
  my_LAT_PIN,
  my_OE_PIN,
  my_CLK_PIN
};

static void resetTickerState(TickerState& ts, const char* symbol) {
  ts.symbol = symbol;
  ts.history.init();
  ts.hasBackfill = false;
  ts.candleDayKey = 0;
  ts.lastPrice = 0.0f;
  ts.hasQuote = false;
  ts.quoteLastPrice = 0.0f;
  ts.lastQuoteFetchMs = 0;
  ts.candleLastPrice = 0.0f;
  ts.prevClose = 0.0f;
  ts.prevCloseDayKey = 0;
  ts.changePercent = 0.0f;
  ts.lastCandleFetchMs = 0;
  ts.sessionBasisDayKey = 0;
  ts.sessionBasisOpen = false;
}

static void applyTickersFromManager() {
  uint8_t n = tickerManager.count();
  if (n == 0) {
    tickerManager.loadDefaults();
    n = tickerManager.count();
  }
  if (n > MAX_TICKERS) n = MAX_TICKERS;

  for (uint8_t i = 0; i < n; i++) {
    resetTickerState(tickers[i], tickerManager.symbolAt(i));
  }
  // Clear unused slots to avoid stale pointers/strings.
  for (uint8_t i = n; i < MAX_TICKERS; i++) {
    resetTickerState(tickers[i], "");
  }

  activeTickerIdx = 0;
  prefetchTickerIdx = nextTickerIndex(activeTickerIdx);
  activeSinceMs = millis();
}

static void onTickersChanged() {
  applyTickersFromManager();
  if (dma_display) displayActive(dma_display, tickers[activeTickerIdx]);
}

void setup() {
  Serial.begin(115200);

  TickerPersistence::loadOrDefaults(tickerManager);
  applyTickersFromManager();

  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nWiFi connected");
  Serial.print("IP: ");
  Serial.println(WiFi.localIP());

  if (MDNS.begin(MdnsName::hostname())) {
    MDNS.addService("http", "tcp", 80);
    Serial.print("mDNS: http://");
    Serial.print(MdnsName::hostname());
    Serial.println(".local/");
  } else {
    Serial.println("mDNS: failed to start");
  }

  HUB75_I2S_CFG mxconfig(64, 32, 1, _pins);
  mxconfig.clkphase = false;

  dma_display = new MatrixPanel_I2S_DMA(mxconfig);
  dma_display->begin();
  dma_display->setPanelBrightness(10);
  dma_display->clearScreen();
  Serial.println("Display initialized");

  WebConfig::begin(tickerManager, onTickersChanged);

  // Helpful in networks where .local mDNS doesn't resolve (some hotspots / Android): show IP briefly.
  {
    String ip = WiFi.localIP().toString();
    displaySplash(dma_display, "Open config:", ip.c_str());
    delay(1200);
  }

  displaySplash(dma_display, "Syncing time", "NTP...");
  while (!syncTimeNtpBlocking(60000)) {
    displaySplash(dma_display, "Syncing time", "retry...");
    delay(1000);
  }

  activeTickerIdx = 0;
  prefetchTickerIdx = nextTickerIndex(activeTickerIdx);

  struct tm nowLocal;
  (void)getLocalTime(&nowLocal, 1000);
  bool sessionOpen = isRegularSessionOpenTm(nowLocal);

  displaySplash(dma_display, "Fetching data", tickers[activeTickerIdx].symbol);
  // Don't try to run multiple API calls back-to-back here; the loop will schedule one call per spacing window.
  (void)ensureQuoteFresh(tickers[activeTickerIdx], nowLocal, sessionOpen);
  displayActive(dma_display, tickers[activeTickerIdx]);
  activeSinceMs = millis();
}

void loop() {
  unsigned long nowMs = millis();
  WebConfig::tick();

  struct tm nowLocal;
  if (!getLocalTime(&nowLocal, 0)) {
    delay(50);
    return;
  }
  bool sessionOpen = isRegularSessionOpenTm(nowLocal);

  uint8_t n = tickerManager.count();
  if (n == 0) {
    delay(50);
    return;
  }

  // Scheduler: run at most ONE API-related operation per spacing window.
  // This avoids immediately attempting quote+time_series back-to-back (which will always throttle the 2nd call).
  if (twelvedataCanRequestNow()) {
    bool did = false;

    // Priority 1: prefetch should have quote first (for correct % using official previous_close),
    // then candles (for the graph). Only one operation per spacing window.
    if (!tickers[prefetchTickerIdx].hasQuote) {
      // #region agent log
      Serial.printf("{\"sessionId\":\"debug-session\",\"runId\":\"run1\",\"hypothesisId\":\"H5\","
                    "\"location\":\"main.cpp:loop\",\"message\":\"do_prefetch_quote\","
                    "\"timestamp\":%lu,\"data\":{\"active\":\"%s\",\"prefetch\":\"%s\"}}\n",
                    (unsigned long)millis(), tickers[activeTickerIdx].symbol, tickers[prefetchTickerIdx].symbol);
      // #endregion
      did = ensureQuoteFresh(tickers[prefetchTickerIdx], nowLocal, sessionOpen);
    } else if (!tickers[prefetchTickerIdx].hasBackfill) {
      // #region agent log
      Serial.printf("{\"sessionId\":\"debug-session\",\"runId\":\"run1\",\"hypothesisId\":\"H4\","
                    "\"location\":\"main.cpp:loop\",\"message\":\"do_prefetch_candles\","
                    "\"timestamp\":%lu,\"data\":{\"active\":\"%s\",\"prefetch\":\"%s\"}}\n",
                    (unsigned long)millis(), tickers[activeTickerIdx].symbol, tickers[prefetchTickerIdx].symbol);
      // #endregion
      did = ensureCandlesFresh(tickers[prefetchTickerIdx], nowLocal, sessionOpen);
    }

    // Priority 2: keep active quote reasonably fresh during session (official prev close + last).
    if (!did) {
      unsigned long age = tickers[activeTickerIdx].hasQuote ? (nowMs - tickers[activeTickerIdx].lastQuoteFetchMs) : 0;
      if (!tickers[activeTickerIdx].hasQuote || (sessionOpen && age > QUOTE_FRESHNESS_MS)) {
        // #region agent log
        Serial.printf("{\"sessionId\":\"debug-session\",\"runId\":\"run1\",\"hypothesisId\":\"H2\","
                      "\"location\":\"main.cpp:loop\",\"message\":\"do_active_quote\","
                      "\"timestamp\":%lu,\"data\":{\"active\":\"%s\",\"ageMs\":%lu,\"sessionOpen\":%d}}\n",
                      (unsigned long)millis(), tickers[activeTickerIdx].symbol, (unsigned long)age, (int)sessionOpen);
        // #endregion
        did = ensureQuoteFresh(tickers[activeTickerIdx], nowLocal, sessionOpen);
      }
    }

    // Priority 3: keep active candles fresh-ish during session for graph.
    if (!did) {
      unsigned long cage = tickers[activeTickerIdx].lastCandleFetchMs ? (nowMs - tickers[activeTickerIdx].lastCandleFetchMs) : 0;
      if (!tickers[activeTickerIdx].hasBackfill || (sessionOpen && cage > CANDLE_FRESHNESS_MS)) {
        // #region agent log
        Serial.printf("{\"sessionId\":\"debug-session\",\"runId\":\"run1\",\"hypothesisId\":\"H2\","
                      "\"location\":\"main.cpp:loop\",\"message\":\"do_active_candles\","
                      "\"timestamp\":%lu,\"data\":{\"active\":\"%s\",\"ageMs\":%lu,\"sessionOpen\":%d}}\n",
                      (unsigned long)millis(), tickers[activeTickerIdx].symbol, (unsigned long)cage, (int)sessionOpen);
        // #endregion
        did = ensureCandlesFresh(tickers[activeTickerIdx], nowLocal, sessionOpen);
      }
    }

    // Priority 4: prefetch quote (so header is live immediately on switch).
    if (!did) {
      unsigned long page = tickers[prefetchTickerIdx].hasQuote ? (nowMs - tickers[prefetchTickerIdx].lastQuoteFetchMs) : 0;
      if (!tickers[prefetchTickerIdx].hasQuote || (sessionOpen && page > QUOTE_FRESHNESS_MS)) {
        // #region agent log
        Serial.printf("{\"sessionId\":\"debug-session\",\"runId\":\"run1\",\"hypothesisId\":\"H2\","
                      "\"location\":\"main.cpp:loop\",\"message\":\"do_prefetch_quote\","
                      "\"timestamp\":%lu,\"data\":{\"prefetch\":\"%s\",\"ageMs\":%lu,\"sessionOpen\":%d}}\n",
                      (unsigned long)millis(), tickers[prefetchTickerIdx].symbol, (unsigned long)page, (int)sessionOpen);
        // #endregion
        did = ensureQuoteFresh(tickers[prefetchTickerIdx], nowLocal, sessionOpen);
      }
    }
  }

  if ((nowMs - activeSinceMs) >= MIN_DISPLAY_MS) {
    const TickerState& nextTs = tickers[prefetchTickerIdx];
    uint32_t basisDayKey = sessionOpen ? dayKeyFromTm(nowLocal) : closedSessionCandidateDayKeyFromTm(nowLocal);
    bool basisOk = nextTs.hasBackfill && nextTs.sessionBasisOpen == sessionOpen && nextTs.sessionBasisDayKey == basisDayKey;
    bool freshOk = (!sessionOpen) || ((nowMs - nextTs.lastCandleFetchMs) <= CANDLE_FRESHNESS_MS);
    if (basisOk && freshOk) {
      activeTickerIdx = prefetchTickerIdx;
      activeSinceMs = nowMs;
      prefetchTickerIdx = nextTickerIndex(activeTickerIdx);
      displayActive(dma_display, tickers[activeTickerIdx]);
    }
  }

  delay(20);
}


