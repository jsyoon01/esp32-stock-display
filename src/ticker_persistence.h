#pragma once

#include <Arduino.h>
#include <Preferences.h>

#include "ticker_manager.h"

// NVS helpers for persisting tickers.
namespace TickerPersistence {
  // Loads tickers from NVS; falls back to defaults if missing/invalid.
  void loadOrDefaults(TickerManager& mgr);

  // Saves current tickers to NVS.
  bool save(const TickerManager& mgr);
}


