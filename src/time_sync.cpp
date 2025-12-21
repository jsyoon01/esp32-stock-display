#include "time_sync.h"

#include "app_config.h"

static void dayKeyToYmd(uint32_t dayKey, int& yOut, unsigned& mOut, unsigned& dOut) {
  yOut = (int)(dayKey / 10000u);
  mOut = (unsigned)((dayKey / 100u) % 100u);
  dOut = (unsigned)(dayKey % 100u);
}

static int64_t daysFromCivil(int y, unsigned m, unsigned d) {
  // Howard Hinnant's algorithm: days relative to 1970-01-01.
  y -= (m <= 2) ? 1 : 0;
  const int era = (y >= 0 ? y : y - 399) / 400;
  const unsigned yoe = (unsigned)(y - era * 400);
  const unsigned doy = (153u * (m + (m > 2 ? (unsigned)-3 : 9)) + 2u) / 5u + d - 1u;
  const unsigned doe = yoe * 365u + yoe / 4u - yoe / 100u + doy;
  return (int64_t)era * 146097LL + (int64_t)doe - 719468LL;
}

static void civilFromDays(int64_t z, int& yOut, unsigned& mOut, unsigned& dOut) {
  z += 719468LL;
  const int era = (z >= 0 ? (int)z : (int)(z - 146096LL)) / 146097;
  const unsigned doe = (unsigned)(z - (int64_t)era * 146097LL);
  const unsigned yoe = (doe - doe / 1460u + doe / 36524u - doe / 146096u) / 365u;
  const int y = (int)yoe + era * 400;
  const unsigned doy = doe - (365u * yoe + yoe / 4u - yoe / 100u);
  const unsigned mp = (5u * doy + 2u) / 153u;
  const unsigned d = doy - (153u * mp + 2u) / 5u + 1u;
  const unsigned m = mp + (mp < 10u ? 3u : (unsigned)-9);
  yOut = y + (m <= 2u ? 1 : 0);
  mOut = m;
  dOut = d;
}

uint32_t addDaysToDayKey(uint32_t dayKey, int deltaDays) {
  int y = 0;
  unsigned m = 1, d = 1;
  dayKeyToYmd(dayKey, y, m, d);
  int64_t z = daysFromCivil(y, m, d);
  z += (int64_t)deltaDays;
  civilFromDays(z, y, m, d);
  return (uint32_t)y * 10000u + (uint32_t)m * 100u + (uint32_t)d;
}

static uint8_t dayOfWeekSunday0(uint32_t dayKey) {
  // 0=Sunday .. 6=Saturday
  int y = 0;
  unsigned m = 1, d = 1;
  dayKeyToYmd(dayKey, y, m, d);
  // 1970-01-01 was Thursday. If Sunday=0, Thursday=4.
  int64_t z = daysFromCivil(y, m, d);
  int dow = (int)((z + 4) % 7);
  if (dow < 0) dow += 7;
  return (uint8_t)dow;
}

uint32_t mostRecentWeekdayDayKey(uint32_t dayKey) {
  while (true) {
    uint8_t dow = dayOfWeekSunday0(dayKey);
    if (dow == 0 || dow == 6) {
      dayKey = addDaysToDayKey(dayKey, -1);
      continue;
    }
    return dayKey;
  }
}

uint32_t previousWeekdayDayKey(uint32_t dayKey) {
  return mostRecentWeekdayDayKey(addDaysToDayKey(dayKey, -1));
}

void formatDayKeyToDate(uint32_t dayKey, char out[11]) {
  uint16_t y = (uint16_t)(dayKey / 10000u);
  uint8_t m = (uint8_t)((dayKey / 100u) % 100u);
  uint8_t d = (uint8_t)(dayKey % 100u);
  snprintf(out, 11, "%04u-%02u-%02u", (unsigned)y, (unsigned)m, (unsigned)d);
}

uint32_t dayKeyFromTm(const struct tm& t) {
  uint16_t y = (uint16_t)(t.tm_year + 1900);
  uint8_t m = (uint8_t)(t.tm_mon + 1);
  uint8_t d = (uint8_t)t.tm_mday;
  return (uint32_t)y * 10000u + (uint32_t)m * 100u + (uint32_t)d;
}

bool isRegularSessionOpenTm(const struct tm& t) {
  if (t.tm_wday == 0 || t.tm_wday == 6) return false;
  int minutes = t.tm_hour * 60 + t.tm_min;
  return (minutes >= MARKET_OPEN_MINUTES) && (minutes < (16 * 60));
}

static bool isAfterRegularCloseTm(const struct tm& t) {
  if (t.tm_wday == 0 || t.tm_wday == 6) return false;
  int minutes = t.tm_hour * 60 + t.tm_min;
  return minutes >= (16 * 60);
}

uint32_t closedSessionCandidateDayKeyFromTm(const struct tm& t) {
  uint32_t today = dayKeyFromTm(t);
  if (isAfterRegularCloseTm(t)) return today;
  return previousWeekdayDayKey(today);
}

bool syncTimeNtpBlocking(uint32_t timeoutMs) {
  configTzTime(NY_TZ_INFO, "pool.ntp.org", "time.nist.gov");
  unsigned long start = millis();
  struct tm timeinfo;
  while ((millis() - start) < timeoutMs) {
    if (getLocalTime(&timeinfo, 1000)) {
      Serial.printf("NTP time synced: %04d-%02d-%02d %02d:%02d:%02d\n",
                    timeinfo.tm_year + 1900, timeinfo.tm_mon + 1, timeinfo.tm_mday,
                    timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);
      return true;
    }
    delay(250);
  }
  Serial.println("NTP sync timeout");
  return false;
}


