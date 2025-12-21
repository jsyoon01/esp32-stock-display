#pragma once

// Centralized constants/config for the stock display sketch.

#include <stdint.h>

// Twelve Data (REST only)
static const char* TWELVEDATA_TIME_SERIES_URL = "https://api.twelvedata.com/time_series";
static const char* TWELVEDATA_QUOTE_URL = "https://api.twelvedata.com/quote";

// Secrets (WiFi + API key)
// Provide your own untracked `src/app_secrets.h`. See `src/app_secrets.example.h`.
#if __has_include("app_secrets.h")
#include "app_secrets.h"
#else
static const char* WIFI_SSID = "";
static const char* WIFI_PASSWORD = "";
static const char* TWELVEDATA_API_KEY = "";
#endif

// Display
#define DISPLAY_HEIGHT 32
#define TEXT_LINE_HEIGHT 8
#define HEADER_HEIGHT (2 * TEXT_LINE_HEIGHT)
#define GRAPH_Y_OFFSET HEADER_HEIGHT
#define GRAPH_HEIGHT (DISPLAY_HEIGHT - HEADER_HEIGHT)
#define DISPLAY_WIDTH 64

// Header padding
#define HEADER_PAD_X 1
#define HEADER_PAD_Y 1
#define HEADER_PAD_RIGHT 1

// Trading minutes (regular session)
#define TRADING_MINUTES 390
#define MARKET_OPEN_MINUTES (9 * 60 + 30)

// API throttling (free tier is tight)
#define HTTP_TIMEOUT_MS 10000
#define API_MIN_SPACING_MS 10000

// Display dwell (only switch once next ticker is ready)
#define MIN_DISPLAY_MS 15000

// Closed-market session lookup (holidays/weekends): max prior weekday dates to probe
#define CLOSED_MARKET_LOOKBACK_DAYS 5

// Candle freshness for the active + next tickers during regular session
#define CANDLE_FRESHNESS_MS (6UL * 60UL * 1000UL)

// Quote freshness (active + next) during regular session
#define QUOTE_FRESHNESS_MS (2UL * 60UL * 1000UL)

// NTP / session timing
static const char* NY_TZ_INFO = "EST5EDT,M3.2.0/2,M11.1.0/2";

// Prices in CandleHistory are stored scaled.
static const int32_t PRICE_SCALE = 1000; // store prices as milli-dollars in int32

// Tickers
#define MAX_TICKERS 10
#define MAX_SYMBOL_LEN 8

// Defaults (used when nothing is saved in NVS or saved data is invalid)
static const char* const DEFAULT_TICKERS[] = {
  "AAPL",
  "MSFT",
  "NVDA",
  "TSLA",
  "AMZN",
  "GOOGL",
  "META",
  "NFLX",
  "AMD",
  "SPY",
};
static const uint8_t DEFAULT_TICKER_COUNT = (uint8_t)(sizeof(DEFAULT_TICKERS) / sizeof(DEFAULT_TICKERS[0]));


