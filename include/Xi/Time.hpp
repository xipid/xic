#ifndef XI_TIME
#define XI_TIME

#include "Primitives.hpp"
#include "String.hpp"

namespace Xi {

i64 epochMicros();
int getGMT();

class XI_EXPORT Time {
private:
  static constexpr i64 US_PER_SEC = 1000000ULL;
  static constexpr i64 US_PER_MIN = 60000000ULL;
  static constexpr i64 US_PER_HOUR = 3600000000ULL;
  static constexpr i64 US_PER_DAY = 86400000000ULL;

  friend i64 epochMicros();
  friend int getGMT();

public:
  int tz = 0;
  i64 us;

  static void sleep(double seconds);
  static bool isLeap(int y) {
    return (y % 4 == 0) && (y % 100 != 0 || y % 400 == 0);
  }
  static int daysInMonth(int m, int y);
  static void civFromDays(long long z, int &y, int &m, int &d, int &doy);
  static long long daysFromCiv(int y, int m, int d);
  static int parse_int(const char *&str, int len);
  static bool ch_eq(char a, char b);

  static void syncPPS();
  static void syncClock(i64 now);
  static void syncClock();

  Time();
  Time(i64 u) : us(u) {}
  Time(const Xi::String &date, const Xi::String &fmt);

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

  Xi::String toString(const Xi::String &fmt = "yyyy/mm/dd hh:mm:ss",
                      int targetTzHours = 0) const;
};

} // namespace Xi

#endif