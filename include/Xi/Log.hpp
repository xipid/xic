/**
 * @file Log.hpp
 * @brief Thread-safe logging utility for the Xi framework.
 */

#ifndef XI_CORE_LOG_HPP
#define XI_CORE_LOG_HPP

#include "../Collection/String.hpp"
#include "Primitives.hpp"

#if !defined(__KERNEL__) && !defined(XI_NO_STD)
#ifndef ARDUINO
#include <unistd.h>
#else
#include <Arduino.h>
#endif
#endif

#ifdef __KERNEL__
#include <linux/printk.h>
#endif

namespace Xi {

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
private:
  alignas(4) unsigned int lock_val = 0;
  LogLevel currentLevel = LogLevel::Info;

  Log() {}
  Log(const Log &) = delete;
  Log &operator=(const Log &) = delete;

  void lock() {
    while (__atomic_test_and_set(&lock_val, __ATOMIC_ACQUIRE)) {
#if defined(__x86_64__) || defined(__i386__)
      __builtin_ia32_pause();
#elif defined(__arm__) || defined(__aarch64__)
      __asm__ volatile("yield" ::: "memory");
#elif defined(ESP_PLATFORM) || defined(FREERTOS_CONFIG_H) ||                   \
    defined(INC_FREERTOS_H)
      vTaskDelay(1);
#elif defined(ARDUINO)
      delayMicroseconds(1);
#else
      sleepU(1);
#endif
    }
  }

  void unlock() { __atomic_clear(&lock_val, __ATOMIC_RELEASE); }

  void print_unlocked(const Xi::String &msg) {
#if defined(__KERNEL__)
    printk(KERN_INFO "%s", msg.c_str());
#elif defined(ARDUINO)
    Serial.print(msg.c_str());
#elif defined(XI_NO_STD) && defined(__linux__) && defined(__x86_64__)
    long ret;
    __asm__ volatile("syscall"
                     : "=a"(ret)
                     : "a"(1), "D"(2), "S"(msg.data()), "d"(msg.length())
                     : "rcx", "r11", "memory");
#else
    ::write(2, msg.data(), msg.length());
#endif
  }

  template <typename T> void print_unlocked(const T &msg) {
    if constexpr (Xi::IsSame<T, Xi::String>::Value) {
      print_unlocked(msg);
    } else if constexpr (Xi::IsSame<T, const char *>::Value ||
                         Xi::IsSame<T, char *>::Value) {
      print_unlocked(Xi::String(msg));
    } else if constexpr (Xi::IsPrimitive<T>::Value) {
      print_unlocked(Xi::String(msg));
    } else if constexpr (Collection::HasToString<T>::value) {
      print_unlocked(msg.toString());
    } else {
      print_unlocked(Xi::serialize(msg));
    }
  }

  void println_unlocked() {
#if defined(__KERNEL__)
    printk(KERN_INFO "\n");
#elif defined(ARDUINO)
    Serial.println();
#elif defined(XI_NO_STD) && defined(__linux__) && defined(__x86_64__)
    long ret;
    __asm__ volatile("syscall"
                     : "=a"(ret)
                     : "a"(1), "D"(2), "S"("\n"), "d"(1)
                     : "rcx", "r11", "memory");
#else
    ::write(2, "\n", 1);
#endif
  }

  template <typename T> void println_unlocked(const T &msg) {
    print_unlocked(msg);
    println_unlocked();
  }

public:
  static Log &getInstance() {
    alignas(4) static unsigned int init_lock = 0;
    static bool initialized = false;
    static alignas(Log) char storage[sizeof(Log)];
    if (!__atomic_load_n(&initialized, __ATOMIC_ACQUIRE)) {
      while (__atomic_test_and_set(&init_lock, __ATOMIC_ACQUIRE)) {
#if defined(__x86_64__) || defined(__i386__)
        __builtin_ia32_pause();
#elif defined(__arm__) || defined(__aarch64__)
        __asm__ volatile("yield" ::: "memory");
#endif
      }
      if (!initialized) {
        new (storage) Log();
        __atomic_store_n(&initialized, true, __ATOMIC_RELEASE);
      }
      __atomic_clear(&init_lock, __ATOMIC_RELEASE);
    }
    return *reinterpret_cast<Log *>(storage);
  }

  void setLevel(LogLevel l) { currentLevel = l; }

  void print(const Xi::String &msg) {
    lock();
    print_unlocked(msg);
    unlock();
  }

  template <typename T> void print(const T &msg) {
    lock();
    print_unlocked(msg);
    unlock();
  }

  void println() {
    lock();
    println_unlocked();
    unlock();
  }

  template <typename T> void println(const T &msg) {
    lock();
    println_unlocked(msg);
    unlock();
  }

  template <typename T> void append(LogLevel l, const T &msg) {
    if (l < currentLevel)
      return;
#if defined(__KERNEL__)
    // Convert generic T to string
    Xi::String str_msg;
    if constexpr (Xi::IsSame<T, Xi::String>::Value) {
      str_msg = msg;
    } else if constexpr (Xi::IsSame<T, const char *>::Value ||
                         Xi::IsSame<T, char *>::Value) {
      str_msg = Xi::String(msg);
    } else if constexpr (Xi::IsPrimitive<T>::Value) {
      str_msg = Xi::String(msg);
    } else if constexpr (Collection::HasToString<T>::value) {
      str_msg = msg.toString();
    } else {
      str_msg = Xi::serialize(msg);
    }
    // Linux kernel printk expects levels to be at the absolute start of format
    // string literals
    switch (l) {
    case LogLevel::Verbose:
      printk(KERN_DEBUG "VERBOSE: %s\n", str_msg.c_str());
      break;
    case LogLevel::Info:
      printk(KERN_INFO "INFO: %s\n", str_msg.c_str());
      break;
    case LogLevel::Warning:
      printk(KERN_WARNING "WARN: %s\n", str_msg.c_str());
      break;
    case LogLevel::Error:
      printk(KERN_ERR "ERROR: %s\n", str_msg.c_str());
      break;
    case LogLevel::Critical:
      printk(KERN_CRIT "CRITICAL: %s\n", str_msg.c_str());
      break;
    default:
      printk(KERN_INFO "%s\n", str_msg.c_str());
      break;
    }
#else
    lock();
    println_unlocked(msg);
    unlock();
#endif
  }

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

} // namespace Xi

#endif // XI_CORE_LOG_HPP
