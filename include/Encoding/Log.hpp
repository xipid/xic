/**
 * @file Log.hpp
 * @brief Thread-safe logging utility for the Xi framework.

 */

#ifndef XI_DATA_LOG_HPP
#define XI_DATA_LOG_HPP

#include <Xi/Primitives.hpp>
#include <Xi/String.hpp>

#ifndef ARDUINO
#include <unistd.h>
#else
#include <Arduino.h>
#endif

/**
 * @namespace Data
 * @brief Contains data serialization and processing utilities.
 */
namespace Data {

using namespace Xi;

/**
 * @enum LogLevel
 * @brief Defines the severity levels for log messages.
 */
enum class XI_EXPORT LogLevel {
  Verbose = 0,  ///< Detailed diagnostic messages.
  Info = 1,     ///< General informational messages.
  Warning = 2,  ///< Potential issues that don't prevent execution.
  Error = 3,    ///< Errors that might allow continued execution.
  Critical = 4, ///< Fatal errors that require immediate attention.
  None = 5      ///< Disables all logging.
};

/**
 * @class Log
 * @brief Singleton logging class providing various output levels.
 */
class XI_EXPORT Log {
public:
  /**
   * @brief Gets the singleton instance of the Log class.
   * @return Reference to the Log instance.
   */
  static Log &getInstance() {
    static Log instance;
    return instance;
  }

  /**
   * @brief Sets the minimum log level for output.
   * @param l The new log level.
   */
  void setLevel(LogLevel l) { currentLevel = l; }

  /**
   * @brief Prints a message to the log output.
   * @param msg The string message to print.
   */
  void print(const Xi::String &msg) {
#ifndef ARDUINO
    ::write(2, msg.data(), msg.length());
#else
    Serial.print(msg.c_str());
#endif
  }

  /**
   * @brief Generic print for non-string types.
   */
  template <typename T> void print(const T &msg) {
#ifndef ARDUINO
    // Fallback for types that don't have specialized print
#else
    Serial.print(msg);
#endif
  }

  /**
   * @brief Prints a newline character.
   */
  void println() {
#ifndef ARDUINO
    ::write(2, "\n", 1);
#else
    Serial.println();
#endif
  }

  /**
   * @brief Prints a value followed by a newline.
   */
  template <typename T> void println(const T &msg) {
    print(msg);
    println();
  }

  /**
   * @brief Appends a message if the level matches the current setting.
   */
  template <typename T> void append(LogLevel l, const T &msg) {
    if (l < currentLevel)
      return;
    println(msg);
  }

  // Console Shortcuts
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
  Log(const Log &) = delete;
  Log &operator=(const Log &) = delete;
};

// Global Shortcuts for ease of use
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

} // namespace Data

#endif // XI_DATA_LOG_HPP