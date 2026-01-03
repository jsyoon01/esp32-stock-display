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
static uint8_t rrTickerIdx = 0;
static unsigned long nextPairAtMs = 0;
static bool wasSessionOpen = false;

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
  activeSinceMs = millis();
  rrTickerIdx = 0;
  nextPairAtMs = 0;
  wasSessionOpen = false;
}

static void preloadTickerBlocking(TickerState& ts, const struct tm& nowLocal) {
  if (!ts.symbol || ts.symbol[0] == '\0') return;

  bool quoteOk = false;
  bool candlesOk = false;
  unsigned long lastUiMs = 0;

  while (!(quoteOk && candlesOk)) {
    WebConfig::tick();
    unsigned long nowMs = millis();

    if (!quoteOk && twelvedataCanRequestNow()) {
      quoteOk = ensureQuoteFresh(ts, nowLocal, false);
    }
    if (!candlesOk && twelvedataCanRequestNow()) {
      candlesOk = ensureCandlesFresh(ts, nowLocal, false);
    }

    if (quoteOk && candlesOk) break;

    // If throttled, wait near the next allowed window; otherwise short sleep.
    unsigned long nextAllowed = twelvedataNextAllowedAtMs();
    if (nextAllowed > nowMs) {
      unsigned long waitMs = nextAllowed - nowMs;
      if (waitMs > 2000UL) waitMs = 2000UL;
      delay(waitMs);
    } else {
      delay(50);
    }

    displaySplash(dma_display, "Loading data", ts.symbol);
  }
}

static void onTickersChanged() {
  applyTickersFromManager();
  if (dma_display) {
    displayActive(dma_display, tickers[activeTickerIdx]);
  }
}

void setup() {
  Serial.begin(115200);

  TickerPersistence::loadOrDefaults(tickerManager);
  applyTickersFromManager();

  // Init display early so we can show WiFi progress/failures (useful when Serial isn't watched).
  HUB75_I2S_CFG mxconfig(64, 32, 1, _pins);
  mxconfig.clkphase = false;

  dma_display = new MatrixPanel_I2S_DMA(mxconfig);
  dma_display->begin();
  dma_display->setPanelBrightness(10);
  dma_display->clearScreen();
  Serial.println("Display initialized");

  displaySplash(dma_display, "Connecting WiFi", WIFI_SSID);

  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  unsigned long wifiStart = millis();
  unsigned long lastUiMs = 0;
  while (WiFi.status() != WL_CONNECTED) {
    delay(250);
    Serial.print(".");

    // Update the matrix every ~1s so the user sees we are alive.
    unsigned long now = millis();
    if (now - lastUiMs >= 1000) {
      lastUiMs = now;
      displaySplash(dma_display, "Connecting WiFi", WIFI_SSID);
    }

    // Timeout with diagnostics.
    if (now - wifiStart > 30000UL) {
      Serial.println("\nWiFi connect timeout");
      Serial.print("WiFi.status=");
      Serial.println((int)WiFi.status());
      displaySplash(dma_display, "WiFi failed", "Check SSID/PW");

      // Keep retrying forever, but with a clearer cadence.
      delay(2000);
      Serial.println("Retrying WiFi...");
      displaySplash(dma_display, "Retrying WiFi", WIFI_SSID);
      WiFi.disconnect(true, true);
      delay(250);
      WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
      // reset timeout window
      wifiStart = millis();
    }
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

  struct tm nowLocal;
  (void)getLocalTime(&nowLocal, 1000);
  bool sessionOpen = isRegularSessionOpenTm(nowLocal);

  uint8_t n = tickerManager.count();

  // If we boot outside session, backfill previous regular session data for all tickers once,
  // then stay quiet until the market opens.
  if (!sessionOpen) {
    if (dma_display) {
      displaySplash(dma_display, "Loading data", tickers[activeTickerIdx].symbol);
    }

    for (uint8_t i = 0; i < n; i++) {
      if (!tickers[i].symbol || tickers[i].symbol[0] == '\0') continue;
      preloadTickerBlocking(tickers[i], nowLocal);
      Serial.print("Preloaded ticker: ");
      Serial.println(tickers[i].symbol);
    }
    activeSinceMs = millis();
  } else {
    // Show something immediately after time sync.
    displayActive(dma_display, tickers[activeTickerIdx]);
    activeSinceMs = millis();
  }
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

  // Market-hours-only fetch: one ticker per minute, quote+time_series back-to-back.
  if (sessionOpen) {
    if (!wasSessionOpen) {
      wasSessionOpen = true;
      rrTickerIdx = activeTickerIdx;
      nextPairAtMs = 0; // run immediately
    }

    if (nowMs >= nextPairAtMs) {
      uint8_t idx = rrTickerIdx;
      if (idx >= n) idx = 0;

      // Best-effort pair. Each call is still subject to the rolling 8/min limiter.
      //
      // Important: if we hit throttling (local limiter or 429 backoff), do NOT advance the
      // round-robin index; instead retry this same ticker as soon as we're allowed again.
      bool quoteOk = ensureQuoteFresh(tickers[idx], nowLocal, true);
      if (!quoteOk && twelvedataNextAllowedAtMs() > nowMs) {
        nextPairAtMs = twelvedataNextAllowedAtMs();
        delay(20);
        return;
      }

      bool candlesOk = ensureCandlesFresh(tickers[idx], nowLocal, true);
      if (!candlesOk && twelvedataNextAllowedAtMs() > nowMs) {
        nextPairAtMs = twelvedataNextAllowedAtMs();
        delay(20);
        return;
      }

      rrTickerIdx = nextTickerIndex(idx);
      nextPairAtMs = nowMs + PAIR_TICK_MS;
    }
  } else {
    wasSessionOpen = false;
  }

  // Always rotate tickers based on dwell; do not stall waiting for prefetch readiness.
  if ((nowMs - activeSinceMs) >= MIN_DISPLAY_MS) {
    activeTickerIdx = nextTickerIndex(activeTickerIdx);
    activeSinceMs = nowMs;
    displayActive(dma_display, tickers[activeTickerIdx]);
  }

  delay(20);
}
