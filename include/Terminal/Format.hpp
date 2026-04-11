/**
 * @file Format.hpp
 * @brief Professional CLI toolkit for the Xi framework.
 *
 * Provides extensive terminal styling, rich UI components (tables, boxes),
 * logging, progress reporting, and system-level terminal control.
 */

#ifndef XI_TERMINAL_FORMAT_HPP
#define XI_TERMINAL_FORMAT_HPP

#include "../Collection/String.hpp"
#include "../Collection/Array.hpp"

using namespace Xi;
using namespace Collection;

namespace Terminal {

// ─── Symbols & Icons ─────────────────────────────────────────────────

struct Icon {
  static const char *Success; // ✔
  static const char *Info;    // ℹ
  static const char *Warning; // ⚠
  static const char *Error;   // ✖
  static const char *Bullet;  // •
  static const char *Arrow;   // →
  static const char *Sparkle; // ✨
};

// ─── ANSI Styling ────────────────────────────────────────────────────

/** @brief Wraps string in ANSI bold escape codes. */
String Bold(const String &s);
/** @brief Wraps string in ANSI italic escape codes. */
String Italic(const String &s);
/** @brief Wraps string in ANSI underline escape codes. */
String Underline(const String &s);
/** @brief Wraps string in ANSI strikethrough escape codes. */
String Strike(const String &s);
/** @brief Wraps string in ANSI dim/faint escape codes. */
String Dim(const String &s);

/** @brief Applies 24-bit RGB foreground color to the string. */
String RGB(const String &s, u8 r, u8 g, u8 b);
/** @brief Applies a linear RGB foreground gradient to the string. */
String RGB(const String &s, u8 r1, u8 g1, u8 b1, u8 r2, u8 g2, u8 b2);

/** @brief Applies 24-bit RGB background color to the string. */
String BRGB(const String &s, u8 r, u8 g, u8 b);
/** @brief Applies a linear RGB background gradient to the string. */
String BRGB(const String &s, u8 r1, u8 g1, u8 b1, u8 r2, u8 g2, u8 b2);

// Standard color shortcuts
String Cyan(const String &s);
String Magenta(const String &s);
String Yellow(const String &s);
String White(const String &s);
String Black(const String &s);
String Gray(const String &s);
String Red(const String &s);
String Green(const String &s);
String Blue(const String &s);

// ─── High-Level Components ───────────────────────────────────────────

/** @brief Formats a terminal hyperlink. */
String Link(const String &text, const String &url);

/** @brief Formats a table. Uses last dimension of array for columns. */
String Table(const Array<String> &arr);

/** @brief Draws a box around content with an optional title. */
String Box(const String &content, const String &title = "");

// ─── Logging & Status ───────────────────────────────────────────────

void Success(const String &msg);
void Info(const String &msg);
void Warn(const String &msg);
void Error(const String &msg);
void Fatal(const String &msg); // Prints and exits

// ─── Progress & Activity ────────────────────────────────────────────

struct LinearTask {
  String total;
  String current;
  String eta;
  bool loading = false;
};

struct BitmapTask : public Array<bool> {};

/**
 * @class Progress
 * @brief Dynamic progress bars and status manager.
 */
class XI_EXPORT Progress {
public:
  String message;
  Array<LinearTask> tasks;
  Array<BitmapTask> bitmapTasks;

  Progress();
  ~Progress();

  void update();
  void destroy();

private:
  bool _started = false;
  void _render();
  void _updateTerminalPrivateMode(int percentage);
};

/**
 * @class Spinner
 * @brief Elegant terminal activity spinner.
 */
class XI_EXPORT Spinner {
public:
  Spinner(const String &msg = "");
  void start(const String &msg = "");
  void update(const String &msg);
  void stop(const String &finalMsg = "", bool success = true);

private:
  String _message;
  int _frame = 0;
  bool _active = false;
  // In a real implementation this would use a background thread/timer
  // For this library we provide a .tick() or similar
};

// ─── Terminal System ────────────────────────────────────────────────

struct WindowSize {
  int rows;
  int cols;
};

WindowSize GetWindowSize();
bool IsTTY(); // Is stdout an interactive terminal?

void Clear();
void ClearLine();
void MoveCursor(int row, int col);
void HideCursor();
void ShowCursor();

/** @brief Scope-based Raw Mode entry/exit. */
class RawMode {
public:
  RawMode();
  ~RawMode();
private:
  void *orig_termios; // opaque handle
};

} // namespace Terminal

#endif // XI_TERMINAL_FORMAT_HPP
