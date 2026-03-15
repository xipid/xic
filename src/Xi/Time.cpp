#include "../../include/Xi/Time.hpp"

#if defined(FREERTOS_CONFIG_H) || defined(INC_FREERTOS_H)
#include <FreeRTOS.h>
#include <task.h>
#elif defined(ARDUINO)
#include <Arduino.h>
#else
#include <time.h>
#if defined(_WIN32)
#include <windows.h>
#endif
#endif

namespace Xi {

i64 epochMicros() {
#if defined(ARDUINO)
  return ::Xi::micros() + Xi::systemStartMicros;
#elif defined(_WIN32)
  unsigned long long ft;
  ::GetSystemTimePreciseAsFileTime(&ft);
  return (ft - 116444736000000000ULL) / 10ULL;
#else
  struct timespec ts;
  clock_gettime(CLOCK_REALTIME, &ts);
  return (i64)ts.tv_sec * 1000000ULL + (i64)(ts.tv_nsec / 1000);
#endif
}

int getGMT() {
#if defined(ARDUINO)
  return 0; // Default or sync from GPS
#else
  time_t t = time(NULL);
  struct tm *lt = localtime(&t);
  struct tm *gt = gmtime(&t);
  return (int)difftime(mktime(lt), mktime(gt)) / 3600;
#endif
}

int Time::parse_int(const char *&str, int len) {
  int v = 0;
  for (int i = 0; i < len; ++i) {
    char c = *str;
    if (c >= '0' && c <= '9') {
      v = v * 10 + (c - '0');
      str++;
    } else
      break;
  }
  return v;
}

bool Time::ch_eq(char a, char b) {
  if (a >= 'A' && a <= 'Z')
    a += 32;
  if (b >= 'A' && b <= 'Z')
    b += 32;
  return a == b;
}

void Time::syncPPS() {
  i64 m = Xi::micros();
  i64 currentEpoch = m + Xi::systemStartMicros;
  i64 nearestSecondMicros =
      ((currentEpoch + 500000ULL) / 1000000ULL) * 1000000ULL;
  Xi::systemStartMicros = nearestSecondMicros - m;
}

void Time::syncClock(i64 now) { Xi::systemStartMicros = now - Xi::micros(); }

void Time::sleep(double seconds) {
#if defined(FREERTOS_CONFIG_H) || defined(INC_FREERTOS_H)
  TickType_t xDelay = static_cast<TickType_t>(seconds * configTICK_RATE_HZ);
  if (xDelay == 0 && seconds > 0)
    xDelay = 1;
  vTaskDelay(xDelay);
#elif defined(ARDUINO)
  ::delay(static_cast<unsigned long>(seconds * 1000.0));
#elif defined(_WIN32)
  ::Sleep(static_cast<DWORD>(seconds * 1000.0));
#else
  struct timespec ts;
  ts.tv_sec = static_cast<time_t>(seconds);
  ts.tv_nsec =
      static_cast<long>((seconds - static_cast<double>(ts.tv_sec)) * 1e9);
  nanosleep(&ts, nullptr);
#endif
}

void Time::civFromDays(long long z, int &y, int &m, int &d, int &doy) {
  z += 719468;
  const long long era = (z >= 0 ? z : z - 146096) / 146097;
  const unsigned doe = static_cast<unsigned>(z - era * 146097);
  const unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
  y = static_cast<int>(yoe) + era * 400;
  doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
  const unsigned mp = (5 * doy + 2) / 153;
  d = doy - (153 * mp + 2) / 5 + 1;
  m = mp + (mp < 10 ? 3 : -9);
  y += (m <= 2);
}

long long Time::daysFromCiv(int y, int m, int d) {
  y -= (m <= 2);
  const long long era = (y >= 0 ? y : y - 399) / 400;
  const unsigned yoe = static_cast<unsigned>(y - era * 400);
  const unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
  const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  return era * 146097 + static_cast<long long>(doe) - 719468;
}

void Time::syncClock() {
#if defined(_WIN32)
  unsigned long long ft;
  ::GetSystemTimePreciseAsFileTime(&ft);
  syncClock((ft - 116444736000000000ULL) / 10ULL);
#else
  struct timespec ts;
  clock_gettime(CLOCK_REALTIME, &ts);
  syncClock((i64)ts.tv_sec * 1000000ULL + (i64)(ts.tv_nsec / 1000));
#endif
}

int Time::daysInMonth(int m, int y) {
  static const u8 days[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
  if (m == 2 && isLeap(y))
    return 29;
  return days[m - 1];
}

int Time::getSecond() const { return (us / US_PER_SEC); }
int Time::getSecondInMinute() const { return (us / US_PER_SEC) % 60; }
void Time::setSecondInMinute(int v) {
  long long baseMin = (us / US_PER_SEC) / 60;
  us = (baseMin * 60 + v) * US_PER_SEC + (us % US_PER_SEC);
}
int Time::getMinute() const { return (us / US_PER_MIN); }
int Time::getMinuteInHour() const { return (us / US_PER_MIN) % 60; }
void Time::setMinuteInHour(int v) {
  long long baseHour = (us / US_PER_MIN) / 60;
  long long secPart = (us / US_PER_SEC) % 60;
  us = ((baseHour * 60 + v) * 60 + secPart) * US_PER_SEC + (us % US_PER_SEC);
}
int Time::getHourInDay() const { return (us / US_PER_HOUR) % 24; }
void Time::setHourInDay(int v) {
  long long days = us / US_PER_DAY;
  long long rem = (us % US_PER_DAY) % US_PER_HOUR;
  us = (days * 86400 + v * 3600) * US_PER_SEC + rem;
}
void Time::getDate(int &y, int &m, int &d, int &doy) const {
  civFromDays(us / US_PER_DAY, y, m, d, doy);
}
int Time::getYear() const {
  int y, m, d, dy;
  getDate(y, m, d, dy);
  return y;
}
int Time::getMonth() const {
  int y, m, d, dy;
  getDate(y, m, d, dy);
  return m;
}
int Time::getDay() const {
  int y, m, d, dy;
  getDate(y, m, d, dy);
  return d;
}
int Time::getDayInYear() const {
  int y, m, d, dy;
  getDate(y, m, d, dy);
  return dy;
}
void Time::setYear(int v) {
  int y, m, d, dy;
  getDate(y, m, d, dy);
  int maxD = daysInMonth(m, v);
  if (d > maxD)
    d = maxD;
  updateDate(v, m, d);
}
void Time::setMonth(int v) {
  int y, m, d, dy;
  getDate(y, m, d, dy);
  int maxD = daysInMonth(v, y);
  if (d > maxD)
    d = maxD;
  updateDate(y, v, d);
}
void Time::setDay(int v) {
  int y, m, d, dy;
  getDate(y, m, d, dy);
  updateDate(y, m, v);
}
void Time::updateDate(int y, int m, int d) {
  us = daysFromCiv(y, m, d) * US_PER_DAY + (us % US_PER_DAY);
}

Time::Time() {
  us = ::Xi::epochMicros();
  tz = ::Xi::getGMT();
}

Time::Time(const Xi::String &date, const Xi::String &fmt) : us(0) {
  const char *s = date.c_str();
  const char *f = fmt.c_str();
  int Y = 1970, M = 1, D = 1, h = 0, mn = 0, sc = 0;
  bool isPM = false;
  int tzH = 0, tzM = 0;
  while (*f) {
    if (*f == 'y' && *(f + 1) == 'y' && *(f + 2) == 'y' && *(f + 3) == 'y') {
      Y = parse_int(s, 4);
      f += 4;
    } else if (*f == 'm' && *(f + 1) == 'm') {
      bool isMinute = false;
      const char *back = f;
      while (back > fmt.c_str()) {
        if (*back == 'h') {
          isMinute = true;
          break;
        }
        back--;
      }
      int val = parse_int(s, 2);
      if (isMinute)
        mn = val;
      else
        M = val;
      f += 2;
    } else if (*f == 'd' && *(f + 1) == 'd') {
      D = parse_int(s, 2);
      f += 2;
    } else if (*f == 'h' && *(f + 1) == 'h') {
      h = parse_int(s, 2);
      f += 2;
    } else if (*f == 's' && *(f + 1) == 's') {
      sc = parse_int(s, 2);
      f += 2;
    } else if (*f == 'r' && *(f + 1) == 'r') {
      if (ch_eq(*s, 'P') && ch_eq(*(s + 1), 'M'))
        isPM = true;
      s += 2;
      f += 2;
    } else if (*f == 'z' && *(f + 1) == 'z') {
      int sign = (*s == '-') ? -1 : 1;
      if (*s == '-' || *s == '+')
        s++;
      tzH = parse_int(s, 2);
      if (*s == ':')
        s++;
      if (*s >= '0' && *s <= '9')
        tzM = parse_int(s, 2);
      tzH *= sign;
      tzM *= sign;
      f += 2;
    } else {
      if (*s == *f)
        s++;
      f++;
    }
  }
  if (isPM && h < 12)
    h += 12;
  if (!isPM && h == 12)
    h = 0;
  us = (daysFromCiv(Y, M, D) * 86400LL + h * 3600LL + mn * 60LL + sc -
        (tzH * 3600LL + tzM * 60LL)) *
       US_PER_SEC;
}

Xi::String Time::toString(const Xi::String &fmt, int targetTzHours) const {
  long long localUs = us + (targetTzHours * 3600 * US_PER_SEC);

  int y, m, d, doy;
  long long days = localUs / US_PER_DAY;
  civFromDays(days, y, m, d, doy);

  long long timeOfDay = localUs % US_PER_DAY;
  int h = (timeOfDay / US_PER_HOUR);
  int mn = (timeOfDay % US_PER_HOUR) / US_PER_MIN;
  int s = (timeOfDay % US_PER_MIN) / US_PER_SEC;

  Xi::String res;
  const char *f = fmt.c_str();

  while (*f) {
    if (*f == 'y' && *(f + 1) == 'y' && *(f + 2) == 'y' && *(f + 3) == 'y') {
      res += y;
      f += 4;
    } else if (*f == 'm' && *(f + 1) == 'm') {
      bool isMin = false;
      const char *back = f;
      while (back > fmt.c_str()) {
        if (*back == 'h') {
          isMin = true;
          break;
        }
        back--;
      }

      int v = isMin ? mn : m;
      if (v < 10)
        res += '0';
      res += v;
      f += 2;
    } else if (*f == 'd' && *(f + 1) == 'd') {
      if (d < 10)
        res += '0';
      res += d;
      f += 2;
    } else if (*f == 'h' && *(f + 1) == 'h') {
      if (h < 10)
        res += '0';
      res += h;
      f += 2;
    } else if (*f == ':' && *(f + 1) == 'm' && *(f + 2) == 'm') {
      res += ':';
      if (mn < 10)
        res += '0';
      res += mn;
      f += 3;
    } else if (*f == 's' && *(f + 1) == 's') {
      if (s < 10)
        res += '0';
      res += s;
      f += 2;
    } else if (*f == 'r' && *(f + 1) == 'r') {
      res += (h >= 12) ? "PM" : "AM";
      f += 2;
    } else if (*f == 'z' && *(f + 1) == 'z') {
      if (targetTzHours >= 0)
        res += '+';
      else
        res += '-';
      int absH = targetTzHours < 0 ? -targetTzHours : targetTzHours;
      if (absH < 10)
        res += '0';
      res += absH;
      res += ":00";
      f += 2;
    } else {
      res += *f;
      f++;
    }
  }
  return res;
}

} // namespace Xi
