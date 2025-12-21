#pragma once

#include <Arduino.h>

#include "ticker_manager.h"

// LAN-only web UI for configuring tickers.
namespace WebConfig {
  typedef void (*OnTickersChangedFn)();
  void begin(TickerManager& mgr, OnTickersChangedFn onChanged);
  void tick();
}


