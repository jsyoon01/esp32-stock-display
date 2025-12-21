#include "ticker_manager.h"

#include <cstring>

TickerManager::TickerManager() : _count(0) {
  for (uint8_t i = 0; i < MAX_TICKERS; i++) {
    _symbols[i][0] = '\0';
  }
  loadDefaults();
}

const char* TickerManager::symbolAt(uint8_t idx) const {
  if (idx >= _count) return "";
  return _symbols[idx];
}

String TickerManager::toCsv() const {
  String out;
  for (uint8_t i = 0; i < _count; i++) {
    if (i) out += ",";
    out += _symbols[i];
  }
  return out;
}

bool TickerManager::isAllowedSymbolChar(char c) {
  if (c >= 'A' && c <= 'Z') return true;
  if (c >= '0' && c <= '9') return true;
  if (c == '.' || c == '-') return true;
  return false;
}

void TickerManager::trimAsciiSpaces(String& s) {
  int start = 0;
  while (start < (int)s.length() && (s[start] == ' ' || s[start] == '\t' || s[start] == '\r' || s[start] == '\n')) start++;
  int end = (int)s.length() - 1;
  while (end >= start && (s[end] == ' ' || s[end] == '\t' || s[end] == '\r' || s[end] == '\n')) end--;
  if (start == 0 && end == (int)s.length() - 1) return;
  if (end < start) {
    s = "";
    return;
  }
  s = s.substring(start, end + 1);
}

String TickerManager::normalizeCsv(const String& csv) {
  // Uppercase + remove spaces around commas.
  String out;
  out.reserve(csv.length());
  for (size_t i = 0; i < csv.length(); i++) {
    char c = csv[i];
    if (c >= 'a' && c <= 'z') c = (char)(c - 'a' + 'A');
    out += c;
  }
  return out;
}

bool TickerManager::parseOneSymbol(const String& tokenIn, char out[MAX_SYMBOL_LEN + 1], String& err) {
  String token = tokenIn;
  trimAsciiSpaces(token);
  if (token.length() < 1 || token.length() > MAX_SYMBOL_LEN) {
    err = "Symbol length must be 1-8";
    return false;
  }
  for (size_t i = 0; i < token.length(); i++) {
    char c = token[i];
    if (c >= 'a' && c <= 'z') c = (char)(c - 'a' + 'A');
    if (!isAllowedSymbolChar(c)) {
      err = "Invalid character in symbol";
      return false;
    }
    out[i] = c;
  }
  out[token.length()] = '\0';
  return true;
}

void TickerManager::loadDefaults() {
  _count = DEFAULT_TICKER_COUNT;
  if (_count > MAX_TICKERS) _count = MAX_TICKERS;
  for (uint8_t i = 0; i < _count; i++) {
    strncpy(_symbols[i], DEFAULT_TICKERS[i], MAX_SYMBOL_LEN);
    _symbols[i][MAX_SYMBOL_LEN] = '\0';
  }
}

bool TickerManager::setFromCsv(const String& csvRaw, String& errorOut) {
  errorOut = "";
  String csv = normalizeCsv(csvRaw);
  trimAsciiSpaces(csv);
  if (csv.length() == 0) {
    errorOut = "Ticker list cannot be empty";
    return false;
  }

  // Parse tokens
  char newSymbols[MAX_TICKERS][MAX_SYMBOL_LEN + 1];
  for (uint8_t i = 0; i < MAX_TICKERS; i++) newSymbols[i][0] = '\0';
  uint8_t newCount = 0;

  int start = 0;
  while (start <= (int)csv.length()) {
    int comma = csv.indexOf(',', start);
    String token;
    if (comma < 0) token = csv.substring(start);
    else token = csv.substring(start, comma);

    if (token.length() > 0 || comma >= 0) {
      if (newCount >= MAX_TICKERS) {
        errorOut = "Too many tickers (max 10)";
        return false;
      }
      String err;
      if (!parseOneSymbol(token, newSymbols[newCount], err)) {
        errorOut = err;
        return false;
      }
      // Dedup (simple)
      for (uint8_t j = 0; j < newCount; j++) {
        if (strcmp(newSymbols[j], newSymbols[newCount]) == 0) {
          errorOut = "Duplicate symbol";
          return false;
        }
      }
      newCount++;
    }

    if (comma < 0) break;
    start = comma + 1;
  }

  if (newCount == 0) {
    errorOut = "Ticker list cannot be empty";
    return false;
  }

  // Apply
  _count = newCount;
  for (uint8_t i = 0; i < _count; i++) {
    strncpy(_symbols[i], newSymbols[i], MAX_SYMBOL_LEN);
    _symbols[i][MAX_SYMBOL_LEN] = '\0';
  }
  return true;
}


