#ifndef XI_LOG_HPP
#define XI_LOG_HPP

#include "Primitives.hpp"
#include "String.hpp"
#include "time.h" // For getting local time strings if needed, or we use Xi::Time

#ifndef ARDUINO
#include <iostream>
#else
#include <Arduino.h>
#endif

namespace Xi {

enum class XI_EXPORT LogLevel {
  Verbose = 0,
  Info = 1,
  Warning = 2,
  Error = 3,
  Critical = 4,
  None = 5
};

class XI_EXPORT Log {
public:
  static Log &getInstance() {
    static Log instance;
    return instance;
  }

  // Configuration
  void setLevel(LogLevel l) { currentLevel = l; }

  void print(const Xi::String &msg) {
#ifndef ARDUINO
    std::cerr << msg.c_str();
#else
    Serial.print(msg.c_str());
#endif
  }

  template <typename T> void print(const T &msg) {
#ifndef ARDUINO
    std::cerr << msg;
#else
    Serial.print(msg);
#endif
  }

  void println() {
#ifndef ARDUINO
    std::cerr << std::endl;
#else
    Serial.println();
#endif
  }

  template <typename T> void println(const T &msg) {
    print(msg);
    println();
  }

  template <typename T> void append(LogLevel l, const T &msg) {
    if (l < currentLevel)
      return;
    println(msg);
  }

  // Shortcuts
  template <typename T> void verbose(const T &msg) {
    append(LogLevel::Verbose, msg);
  }
  template <typename T> void info(const T &msg) { append(LogLevel::Info, msg); }
  template <typename T> void warn(const T &msg) {
    append(LogLevel::Warning, msg);
  }
  template <typename T> void error(const T &msg) {
    append(LogLevel::Error, msg);
  }
  template <typename T> void critical(const T &msg) {
    append(LogLevel::Critical, msg);
  }

private:
  LogLevel currentLevel = LogLevel::Info;

  Log() {}
};

// Global Shortcuts
template <typename T> inline void print(const T &msg) {
  Log::getInstance().print(msg);
}

template <typename T> inline void println(const T &msg) {
  Log::getInstance().println(msg);
}

inline void println() { Log::getInstance().println(); }

template <typename T> inline void info(const T &msg) {
  Log::getInstance().info(msg);
}
template <typename T> inline void warn(const T &msg) {
  Log::getInstance().warn(msg);
}
template <typename T> inline void error(const T &msg) {
  Log::getInstance().error(msg);
}

} // namespace Xi

#endif // XI_LOG_HPP