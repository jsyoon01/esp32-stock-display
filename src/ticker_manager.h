#pragma once

#include <Arduino.h>
#include <stdint.h>

#include "app_config.h"

// Manages a mutable ticker list (max MAX_TICKERS), suitable for NVS persistence and web updates.
class TickerManager {
public:
  TickerManager();

  uint8_t count() const { return _count; }
  const char* symbolAt(uint8_t idx) const;

  // Returns a comma-separated string (no spaces).
  String toCsv() const;

  // Parse and apply a CSV string like "AAPL,MSFT,NVDA".
  // Returns true if applied; false if invalid (and leaves current list unchanged).
  bool setFromCsv(const String& csv, String& errorOut);

  // Load defaults (compile-time)
  void loadDefaults();

private:
  uint8_t _count;
  char _symbols[MAX_TICKERS][MAX_SYMBOL_LEN + 1];

  static bool isAllowedSymbolChar(char c);
  static void trimAsciiSpaces(String& s);
  static String normalizeCsv(const String& csv);
  static bool parseOneSymbol(const String& token, char out[MAX_SYMBOL_LEN + 1], String& err);
};


