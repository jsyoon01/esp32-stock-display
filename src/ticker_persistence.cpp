#include "ticker_persistence.h"

namespace {
  static const char* NVS_NS = "stocks";
  static const char* NVS_KEY = "tickers_csv";
}

void TickerPersistence::loadOrDefaults(TickerManager& mgr) {
  Preferences prefs;
  if (!prefs.begin(NVS_NS, true)) {
    mgr.loadDefaults();
    return;
  }

  String csv = prefs.getString(NVS_KEY, "");
  prefs.end();

  String err;
  if (csv.length() == 0) {
    mgr.loadDefaults();
    return;
  }
  if (!mgr.setFromCsv(csv, err)) {
    mgr.loadDefaults();
    return;
  }
}

bool TickerPersistence::save(const TickerManager& mgr) {
  Preferences prefs;
  if (!prefs.begin(NVS_NS, false)) return false;
  String csv = mgr.toCsv();
  size_t written = prefs.putString(NVS_KEY, csv);
  prefs.end();
  return written > 0;
}


