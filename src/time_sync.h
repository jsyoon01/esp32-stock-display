#pragma once

#include <Arduino.h>
#include <time.h>

// NTP sync (NY timezone configured internally)
bool syncTimeNtpBlocking(uint32_t timeoutMs);

// Session helpers (NY local time)
uint32_t dayKeyFromTm(const struct tm& t);
bool isRegularSessionOpenTm(const struct tm& t);
uint32_t closedSessionCandidateDayKeyFromTm(const struct tm& t);

// DayKey helpers (YYYYMMDD arithmetic)
uint32_t addDaysToDayKey(uint32_t dayKey, int deltaDays);
uint32_t mostRecentWeekdayDayKey(uint32_t dayKey);
uint32_t previousWeekdayDayKey(uint32_t dayKey);
void formatDayKeyToDate(uint32_t dayKey, char out[11]); // "YYYY-MM-DD"


