/**
 * @file Time.hpp
 * @brief High-precision time and date utilities for the Xi framework.

 */

#ifndef XI_CORE_TIME_HPP
#define XI_CORE_TIME_HPP

#include "../Collection/String.hpp"
#include "Primitives.hpp"

using namespace Collection;

namespace Xi {

/** @brief Returns current system time in microseconds since Unix epoch. */
i64 epochMicros();
/** @brief Returns global GMT offset in seconds. */
int getGMT();

/**
 * @class Time
 * @brief High-precision timestamp and calendar utility.
 *
 * Stores time in microseconds since Unix epoch and provides extensive
 * conversion and manipulation methods.
 */
class XI_EXPORT Time {
private:
  static constexpr i64 US_PER_SEC = 1000000ULL;
  static constexpr i64 US_PER_MIN = 60000000ULL;
  static constexpr i64 US_PER_HOUR = 3600000000ULL;
  static constexpr i64 US_PER_DAY = 86400000000ULL;

  friend i64 epochMicros();
  friend int getGMT();

public:
  int tz = 0; ///< Timezone offset in seconds.
  i64 us;     ///< Microseconds since Unix epoch.

  /** @brief Checks if a year is a leap year. */
  static bool isLeap(int y) {
    return (y % 4 == 0) && (y % 100 != 0 || y % 400 == 0);
  }
  static int daysInMonth(int m, int y);
  static void civFromDays(long long z, int &y, int &m, int &d, int &doy);
  static long long daysFromCiv(int y, int m, int d);

  /** @brief Synchronizes the global clock with a specific value. */
  static void syncClock(i64 now);
  /** @brief Synchronizes the global clock with system time. */
  static void syncClock();

  /** @brief Default constructor (uninitialized or zero). */
  Time() : us(0), tz(0) {}
  /** @brief Constructs from raw microseconds and optional timezone. */
  Time(i64 u, int t = 0) : us(u), tz(t) {}
  /** @brief Parses a date string based on a format. */
  Time(const String &date, const String &fmt);

  /**
   * @struct Property
   * @brief Helper for Python-like property syntax (e.g., time.year = 2024).
   */
  template <typename Owner, int (Owner::*Getter)() const,
            void (Owner::*Setter)(int)>
  struct Property {
    Owner *self;
    Property(Owner *s) : self(s) {}
    operator int() const { return (self->*Getter)(); }
    Property &operator=(int v) {
      (self->*Setter)(v);
      return *this;
    }
    Property &operator+=(int v) {
      (self->*Setter)((self->*Getter)() + v);
      return *this;
    }
    Property &operator-=(int v) {
      (self->*Setter)((self->*Getter)() - v);
      return *this;
    }
  };

  int getUsPart() const { return us % US_PER_SEC; }
  void setUsPart(int v) { us = (us / US_PER_SEC) * US_PER_SEC + v; }
  int getSecond() const;
  int getSecondInMinute() const;
  void setSecondInMinute(int v);
  int getMinute() const;
  int getMinuteInHour() const;
  void setMinuteInHour(int v);
  int getHourInDay() const;
  void setHourInDay(int v);
  void getDate(int &y, int &m, int &d, int &doy) const;
  int getYear() const;
  int getMonth() const;
  int getDay() const;
  int getDayInYear() const;
  void setYear(int v);
  void setMonth(int v);
  void setDay(int v);
  void updateDate(int y, int m, int d);

  Property<Time, &Time::getYear, &Time::setYear> year{this};
  Property<Time, &Time::getMonth, &Time::setMonth> month{this};
  Property<Time, &Time::getDay, &Time::setDay> day{this};
  Property<Time, &Time::getHourInDay, &Time::setHourInDay> hour{this};
  Property<Time, &Time::getMinuteInHour, &Time::setMinuteInHour> minute{this};
  Property<Time, &Time::getSecondInMinute, &Time::setSecondInMinute> second{
      this};

  /**
   * @brief Formats the time into a string.
   */
  String toString(const String &fmt = "yyyy/mm/dd hh:mm:ss",
                  int targetTzHours = 0) const;
};

} // namespace Xi

#endif // XI_CORE_TIME_HPP