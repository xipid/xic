/**
 * @file Format.cpp
 * @brief Professional CLI toolkit implementation.
 */

#include "../../include/Terminal/Format.hpp"
#include <cstdio>
#include <cstdlib>
#include <sys/ioctl.h>
#include <unistd.h>
#include <termios.h>
#include <chrono>

static u64 getEpochMs() {
  return std::chrono::duration_cast<std::chrono::milliseconds>(
      std::chrono::steady_clock::now().time_since_epoch()
  ).count();
}

namespace Terminal {

static usz _visualLength(const String &s) {
  usz len = 0;
  bool inEscape = false;
  for (usz i = 0; i < s.length(); ++i) {
    if (s.charAt(i) == '\x1b') inEscape = true;
    else if (inEscape && (s.charAt(i) == 'm' || s.charAt(i) == '\\')) inEscape = false;
    else if (!inEscape) len++;
  }
  return len;
}

// ─── Symbols & Icons ─────────────────────────────────────────────────

const char *Icon::Success = "✔";
const char *Icon::Info    = "ℹ";
const char *Icon::Warning = "⚠";
const char *Icon::Error   = "✖";
const char *Icon::Bullet  = "•";
const char *Icon::Arrow   = "→";
const char *Icon::Sparkle = "✨";

// ─── ANSI Styling ────────────────────────────────────────────────────

String Bold(const String &s) { return "\x1b[1m" + s + "\x1b[22m"; }
String Italic(const String &s) { return "\x1b[3m" + s + "\x1b[23m"; }
String Underline(const String &s) { return "\x1b[4m" + s + "\x1b[24m"; }
String Strike(const String &s) { return "\x1b[9m" + s + "\x1b[29m"; }
String Dim(const String &s) { return "\x1b[2m" + s + "\x1b[22m"; }

String RGB(const String &s, u8 r, u8 g, u8 b) {
  return "\x1b[38;2;" + String((int)r) + ";" + String((int)g) + ";" + String((int)b) + "m" + s + "\x1b[0m";
}

String RGB(const String &s, u8 r1, u8 g1, u8 b1, u8 r2, u8 g2, u8 b2) {
  String res;
  usz len = s.length();
  if (len == 0) return "";
  for (usz i = 0; i < len; ++i) {
    float t = (len > 1) ? (float)i / (float)(len - 1) : 0.0f;
    u8 r = (u8)(r1 + (r2 - r1) * t);
    u8 g = (u8)(g1 + (g2 - g1) * t);
    u8 b = (u8)(b1 + (b2 - b1) * t);
    res += "\x1b[38;2;" + String((int)r) + ";" + String((int)g) + ";" + String((int)b) + "m";
    res += s.charAt(i);
  }
  res += "\x1b[0m";
  return res;
}

String BRGB(const String &s, u8 r, u8 g, u8 b) {
  return "\x1b[48;2;" + String((int)r) + ";" + String((int)g) + ";" + String((int)b) + "m" + s + "\x1b[0m";
}

String BRGB(const String &s, u8 r1, u8 g1, u8 b1, u8 r2, u8 g2, u8 b2) {
  String res;
  usz len = s.length();
  if (len == 0) return "";
  for (usz i = 0; i < len; ++i) {
    float t = (len > 1) ? (float)i / (float)(len - 1) : 0.0f;
    u8 r = (u8)(r1 + (r2 - r1) * t);
    u8 g = (u8)(g1 + (g2 - g1) * t);
    u8 b = (u8)(b1 + (b2 - b1) * t);
    res += "\x1b[48;2;" + String((int)r) + ";" + String((int)g) + ";" + String((int)b) + "m";
    res += s.charAt(i);
  }
  res += "\x1b[0m";
  return res;
}

String Cyan(const String &s) { return RGB(s, 0, 255, 255); }
String Magenta(const String &s) { return RGB(s, 255, 0, 255); }
String Yellow(const String &s) { return RGB(s, 255, 255, 0); }
String White(const String &s) { return RGB(s, 255, 255, 255); }
String Black(const String &s) { return RGB(s, 0, 0, 0); }
String Gray(const String &s) { return RGB(s, 128, 128, 128); }
String Red(const String &s) { return RGB(s, 255, 0, 0); }
String Green(const String &s) { return RGB(s, 0, 255, 0); }
String Blue(const String &s) { return RGB(s, 0, 0, 255); }

// ─── High-Level Components ───────────────────────────────────────────

String Link(const String &text, const String &url) {
  return "\x1b]8;;" + url + "\x1b\\" + text + "\x1b]8;;\x1b\\";
}

String Table(const Array<String> &arr) {
  usz cols = 1;
  if (arr.rank() > 1 && arr._dims) cols = arr._dims[arr.rank() - 1];

  Array<usz> colWidths;
  colWidths.allocate(cols);
  for (usz i = 0; i < cols; i++) colWidths[i] = 0;

  usz rows = (arr.size() + cols - 1) / cols;
  for (usz r = 0; r < rows; ++r) {
    for (usz c = 0; c < cols; ++c) {
      usz idx = r * cols + c;
      if (idx < arr.size()) {
        usz len = _visualLength(arr[idx]);
        if (len > colWidths[c]) colWidths[c] = len;
      }
    }
  }

  String res;
  for (usz r = 0; r < rows; ++r) {
    for (usz c = 0; c < cols; ++c) {
      usz idx = r * cols + c;
      String val = (idx < arr.size()) ? arr[idx] : "";
      res += val.padEnd(colWidths[c] + 2 + (val.length() - _visualLength(val)));
    }
    res += "\n";
  }
  return res;
}

String Box(const String &content, const String &title) {
  Array<String> lines = content.split("\n");
  usz maxLen = _visualLength(title);
  for (usz i = 0; i < lines.size(); ++i) {
    usz vln = _visualLength(lines[i]);
    if (vln > maxLen) maxLen = vln;
  }

  String res;
  // Top border
  res += "┌─";
  if (!title.isEmpty()) {
    res += " " + Bold(title) + " ";
    for (usz i = 0; i < maxLen + 1 - _visualLength(title); ++i) res += "─";
  } else {
    for (usz i = 0; i < maxLen + 3; ++i) res += "─";
  }
  res += "┐\n";

  // Content
  for (usz i = 0; i < lines.size(); ++i) {
    usz vln = _visualLength(lines[i]);
    res += "│  " + lines[i].padEnd(maxLen + (lines[i].length() - vln)) + "  │\n";
  }

  // Bottom border
  res += "└";
  for (usz i = 0; i < maxLen + 4; ++i) res += "─";
  res += "┘\n";

  return res;
}

// ─── Logging & Status ───────────────────────────────────────────────

void Success(const String &msg) { printf("%s  %s\n", Green(Icon::Success).c_str(), msg.c_str()); }
void Info(const String &msg)    { printf("%s  %s\n", Blue(Icon::Info).c_str(), msg.c_str()); }
void Warn(const String &msg)    { printf("%s  %s\n", Yellow(Icon::Warning).c_str(), msg.c_str()); }
void Error(const String &msg)   { printf("%s  %s\n", Red(Icon::Error).c_str(), msg.c_str()); }
void Fatal(const String &msg)   { printf("%s  %s\n", Red(Bold(Icon::Error)).c_str(), Bold(msg).c_str()); exit(1); }

// ─── Progress ───────────────────────────────────────────────────────

Progress::Progress() {}
Progress::~Progress() { destroy(); }

void Progress::update() {
  if (!_started) {
    _started = true;
    HideCursor();
    _hiddenCursor = true;
  }
  _render();
}

void Progress::destroy() {
  if (_started) {
    _started = false;
    _updateTerminalPrivateMode(0);
    
    int lines = (int)(tasks.size() + bitmapTasks.size());
    if (!message.isEmpty()) lines += 1;
    if (lines > 0) {
        printf("\x1b[%dB", lines);
    }
    printf("\n");
    
    if (_hiddenCursor) {
      ShowCursor();
      _hiddenCursor = false;
    }
    fflush(stdout);
  }
}

usz Progress::addLinearTask(u64 total, const String& unit, const String& msg) {
  LinearTask t;
  t.totalRaw = total;
  t.unit = unit;
  t.message = msg;
  t.startTime = getEpochMs();
  t.lastUpdateTime = t.startTime;
  tasks.push(t);
  return tasks.size() - 1;
}

void Progress::updateLinearTask(usz taskIdx, u64 current, const String& msg) {
  if (taskIdx >= tasks.size()) return;
  auto& t = tasks[taskIdx];
  t.currentRaw = current;
  if (!msg.isEmpty()) t.message = msg;
  t.lastUpdateTime = getEpochMs();
}

void Progress::addLinearTaskDelta(usz taskIdx, u64 delta, const String& msg) {
  if (taskIdx >= tasks.size()) return;
  auto& t = tasks[taskIdx];
  t.currentRaw += delta;
  if (!msg.isEmpty()) t.message = msg;
  t.lastUpdateTime = getEpochMs();
}

String Progress::_formatSize(u64 bytes) {
  double val = (double)bytes;
  const char* units[] = {"B", "KB", "MB", "GB", "TB"};
  int uIdx = 0;
  while (val >= 1024.0 && uIdx < 4) {
    val /= 1024.0;
    uIdx++;
  }
  char buf[64];
  snprintf(buf, sizeof(buf), "%.1f %s", val, units[uIdx]);
  return String(buf);
}

String Progress::_formatTime(double seconds) {
  if (seconds < 0.0 || seconds > 3600.0 * 24.0 * 365.0) return "--:--";
  int h = (int)(seconds / 3600);
  int m = (int)((seconds - h * 3600) / 60);
  int s = (int)(seconds - h * 3600 - m * 60);
  char buf[64];
  if (h > 0) {
    snprintf(buf, sizeof(buf), "%02d:%02d:%02d", h, m, s);
  } else {
    snprintf(buf, sizeof(buf), "%02d:%02d", m, s);
  }
  return String(buf);
}

void Progress::_render() {
  printf("\r\x1b[J");
  if (!message.isEmpty()) {
      printf("%s\n", message.c_str());
  }

  int totalPct = 0;
  int count = 0;

  for (usz i = 0; i < tasks.size(); ++i) {
    auto &t = tasks[i];
    
    if (t.startTime == 0) {
        t.startTime = getEpochMs();
    }
    
    u64 now = getEpochMs();
    double elapsed = (now - t.startTime) / 1000.0;
    if (elapsed > 0.05) {
        t.speed = (double)t.currentRaw / elapsed;
    }
    
    double pct = (t.totalRaw > 0) ? (double)t.currentRaw / t.totalRaw : 0.0;
    if (pct > 1.0) pct = 1.0;
    
    double etaVal = 0.0;
    if (t.speed > 0.0 && t.totalRaw > t.currentRaw) {
        etaVal = (double)(t.totalRaw - t.currentRaw) / t.speed;
    }

    t.current = (t.unit == "B") ? _formatSize(t.currentRaw) : String::from((long long)t.currentRaw);
    t.total = (t.unit == "B") ? _formatSize(t.totalRaw) : String::from((long long)t.totalRaw);
    t.eta = _formatTime(elapsed) + " < " + _formatTime(etaVal);

    int barWidth = 25;
    int filled = (int)(pct * barWidth);
    const char* blocks[] = {"░", "▏", "▎", "▍", "▌", "▋", "▊", "▉", "█"};
    
    String filledStr;
    for (int j = 0; j < filled; ++j) filledStr += "█";
    if (filled < barWidth) {
        double remainder = (pct * barWidth) - filled;
        int chunk = (int)(remainder * 8);
        if (chunk > 8) chunk = 8;
        if (chunk < 0) chunk = 0;
        filledStr += blocks[chunk];
    }
    String emptyStr;
    for (int j = filled + 1; j < barWidth; ++j) emptyStr += "░";
    
    String bar = "[" + Cyan(filledStr) + Gray(emptyStr) + "]";
    String pctText = String::from((int)(pct * 100)) + "%";
    
    String speedText;
    if (t.speed > 0) {
        speedText = (t.unit == "B") ? (_formatSize((u64)t.speed) + "/s") : (String::from((long long)t.speed) + " " + t.unit + "/s");
    } else {
        speedText = "--/s";
    }

    String msgText = t.message;
    if (!msgText.isEmpty()) {
      if (msgText.length() > 35) {
        msgText = msgText.substring(0, 32) + "...";
      } else {
        msgText = msgText.padEnd(35);
      }
    }

    printf("  %s%s %s | %s/%s | %s | %s\n",
           msgText.isEmpty() ? "" : (msgText + " ").c_str(),
           bar.c_str(),
           Bold(pctText).c_str(),
           t.current.c_str(), t.total.c_str(),
           Yellow(speedText).c_str(),
           Gray(t.eta).c_str());

    totalPct += (int)(pct * 100);
    count++;
  }

  for (usz i = 0; i < bitmapTasks.size(); ++i) {
    const auto &bt = bitmapTasks[i];
    printf("  ▕");
    usz set = 0;
    for (usz j = 0; j < bt.size(); ++j) {
      printf("%s", bt[j] ? "█" : "░");
      if (bt[j]) set++;
    }
    printf("▏\n");
    if (bt.size() > 0) totalPct += (int)(set * 100 / bt.size());
    count++;
  }

  int lines = (int)(tasks.size() + bitmapTasks.size());
  if (!message.isEmpty()) lines += 1;
  if (lines > 0) {
      printf("\x1b[%dA", lines);
  }

  fflush(stdout);
  if (count > 0) _updateTerminalPrivateMode(totalPct / count);
}

void Progress::_updateTerminalPrivateMode(int pct) {
  if (pct > 0) printf("\x1b]9;4;1;%d\x1b\\", pct);
  else printf("\x1b]9;4;0;0\x1b\\");
  fflush(stdout);
}

// ─── Spinner ─────────────────────────────────────────────────────────

Spinner::Spinner(const String &msg) : _message(msg) {}

void Spinner::start(const String &msg) {
  if (_active) return;
  _active = true;
  if (!msg.isEmpty()) _message = msg;
  HideCursor();
}

void Spinner::update(const String &msg) {
  static const char *frames[] = {"⠋", "⠙", "⠹", "⠸", "⠼", "⠴", "⠦", "⠧", "⠇", "⠏"};
  _message = msg;
  printf("\r\x1b[K%s %s ", Cyan(frames[_frame % 10]).c_str(), _message.c_str());
  fflush(stdout);
  _frame++;
}

void Spinner::stop(const String &finalMsg, bool success) {
  if (!_active) return;
  _active = false;
  String icon = success ? Green(Icon::Success) : Red(Icon::Error);
  printf("\r\x1b[K%s %s\n", icon.c_str(), finalMsg.isEmpty() ? _message.c_str() : finalMsg.c_str());
  ShowCursor();
}

// ─── Terminal System ────────────────────────────────────────────────

WindowSize GetWindowSize() {
  struct winsize w;
  ioctl(STDOUT_FILENO, TIOCGWINSZ, &w);
  return {w.ws_row, w.ws_col};
}

bool IsTTY() { return isatty(STDOUT_FILENO); }

void Clear() { printf("\x1b[2J\x1b[H"); fflush(stdout); }
void ClearLine() { printf("\x1b[2K\r"); fflush(stdout); }
void MoveCursor(int r, int c) { printf("\x1b[%d;%dH", r, c); fflush(stdout); }
void HideCursor() { printf("\x1b[?25l"); fflush(stdout); }
void ShowCursor() { printf("\x1b[?25h"); fflush(stdout); }

RawMode::RawMode() {
  struct termios *raw = new struct termios;
  tcgetattr(STDIN_FILENO, raw);
  orig_termios = raw;
  struct termios next = *raw;
  next.c_lflag &= ~(ECHO | ICANON);
  tcsetattr(STDIN_FILENO, TCSAFLUSH, &next);
}

RawMode::~RawMode() {
  struct termios *orig = (struct termios *)orig_termios;
  tcsetattr(STDIN_FILENO, TCSAFLUSH, orig);
  delete orig;
}

} // namespace Terminal
