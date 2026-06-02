/**
 * @file Prompt.cpp
 * @brief Implementation of interactive terminal prompts.
 */

#include "../../include/Terminal/Prompt.hpp"
#include "../../include/Terminal/Format.hpp"
#include <cstdio>
#include <termios.h>
#include <unistd.h>

namespace Terminal {
namespace Prompt {

String ask(const String &question, const String &defaultVal) {
  printf("%s%s ", question.c_str(),
         defaultVal.isEmpty() ? ":" : (" [" + defaultVal + "]:").c_str());
  fflush(stdout);

  char buf[1024];
  if (!fgets(buf, sizeof(buf), stdin))
    return defaultVal;

  String res(buf);
  res = res.trim();
  return res.isEmpty() ? defaultVal : res;
}

String password(const String &question) {
  String res;
  {
    RawMode raw;
    printf("%s: ", question.c_str());
    fflush(stdout);

    char c;
    while (read(STDIN_FILENO, &c, 1) == 1 && c != '\n' && c != '\r') {
      if (c == 127 || c == 8) { // Backspace
        if (res.length() > 0)
          res = res.substring(0, res.length() - 1);
      } else {
        res += c;
      }
    }
    printf("\n");
  }
  return res;
}

bool confirm(const String &question, bool defaultVal) {
  String suffix = defaultVal ? " [Y/n]" : " [y/N]";
  String res = ask(question + suffix);
  if (res.isEmpty())
    return defaultVal;
  String choice = res.toLowerCase();
  if (choice == "y" || choice == "yes")
    return true;
  if (choice == "n" || choice == "no")
    return false;
  return defaultVal;
}

usz select(const String &question, const Array<String> &options) {
  if (options.size() == 0)
    return 0;
  usz selected = 0;
  {
    RawMode raw;
    HideCursor();
    while (true) {
      printf("\r\x1b[J%s\n", Bold(question).c_str());
      for (usz i = 0; i < options.size(); ++i) {
        if (i == selected) {
          printf(" %s %s\n", Green(Icon::Arrow).c_str(),
                 Cyan(options[i]).c_str());
        } else {
          printf("   %s\n", options[i].c_str());
        }
      }

      fflush(stdout);

      char c;
      if (read(STDIN_FILENO, &c, 1) != 1)
        break;
      if (c == '\n' || c == '\r')
        break;
      if (c == '\x1b') { // Escape sequence
        char seq[2];
        if (read(STDIN_FILENO, &seq[0], 1) != 1)
          break;
        if (read(STDIN_FILENO, &seq[1], 1) != 1)
          break;
        if (seq[0] == '[') {
          if (seq[1] == 'A') { // Up
            if (selected > 0)
              selected--;
            else
              selected = options.size() - 1;
          } else if (seq[1] == 'B') { // Down
            if (selected < options.size() - 1)
              selected++;
            else
              selected = 0;
          }
        }
      }
      // Move cursor back up
      printf("\x1b[%dF", (int)options.size() + 1);
    }
    ShowCursor();
    printf("\r\x1b[J%s %s %s\n", Green(Icon::Success).c_str(), question.c_str(),
           Cyan(options[selected]).c_str());
  }
  return selected;
}

Array<usz> multiSelect(const String &question, const Array<String> &options) {
  Array<usz> results;
  if (options.size() == 0)
    return results;
  Array<bool> selected_mask;
  selected_mask.allocate(options.size());
  for (usz i = 0; i < options.size(); i++)
    selected_mask[i] = false;

  usz cursor = 0;
  {
    RawMode raw;
    HideCursor();
    while (true) {
      printf("\r\x1b[J%s %s\n", Bold(question).c_str(),
             Gray("(Space to toggle, Enter to confirm)").c_str());
      for (usz i = 0; i < options.size(); ++i) {
        String checkbox = selected_mask[i] ? Green("[x]") : "[ ]";
        if (i == cursor) {
          printf(" %s %s %s\n", Cyan(Icon::Arrow).c_str(), checkbox.c_str(),
                 Cyan(options[i]).c_str());
        } else {
          printf("     %s %s\n", checkbox.c_str(), options[i].c_str());
        }
      }

      fflush(stdout);

      char c;
      if (read(STDIN_FILENO, &c, 1) != 1)
        break;
      if (c == '\n' || c == '\r')
        break;
      if (c == ' ') {
        selected_mask[cursor] = !selected_mask[cursor];
      } else if (c == '\x1b') {
        char seq[2];
        if (read(STDIN_FILENO, &seq[0], 1) != 1)
          break;
        if (read(STDIN_FILENO, &seq[1], 1) != 1)
          break;
        if (seq[0] == '[') {
          if (seq[1] == 'A') {
            if (cursor > 0)
              cursor--;
            else
              cursor = options.size() - 1;
          } else if (seq[1] == 'B') {
            if (cursor < options.size() - 1)
              cursor++;
            else
              cursor = 0;
          }
        }
      }
      printf("\x1b[%dF", (int)options.size() + 1);
    }
    ShowCursor();

    String summary;
    for (usz i = 0; i < options.size(); ++i) {
      if (selected_mask[i]) {
        results.push(i);
        if (!summary.isEmpty())
          summary += ", ";
        summary += options[i];
      }
    }
    printf("\r\x1b[J%s %s %s\n", Green(Icon::Success).c_str(), question.c_str(),
           Cyan(summary).c_str());
  }
  return results;
}
String readLine(const String &prompt, Array<String> *history) {
  if (!isatty(STDIN_FILENO)) {
    char buf[4096];
    if (!fgets(buf, sizeof(buf), stdin))
      return "\x04";
    String res(buf);
    if (res.endsWith("\n"))
      res = res.substring(0, res.length() - 1);
    if (res.endsWith("\r"))
      res = res.substring(0, res.length() - 1);
    return res;
  }

  String res;
  int cursor = 0;
  int historyIdx = history ? history->size() : 0;
  String savedCurrent;

  RawMode raw;
  while (true) {
    String suggestion;
    if (history && cursor == res.length() && !res.isEmpty()) {
      for (long long idx = (long long)history->size() - 1; idx >= 0; --idx) {
        const String& h = (*history)[idx];
        if (h.startsWith(res) && h.length() > res.length()) {
          suggestion = h.substring(res.length());
          break;
        }
      }
    }

    printf("\r\x1b[2K%s%s%s", prompt.c_str(), res.c_str(), Gray(suggestion).c_str());
    int moveBack = (int)(res.length() - cursor) + (int)suggestion.length();
    if (moveBack > 0) {
      printf("\x1b[%dD", moveBack);
    }
    fflush(stdout);

    char c;
    if (read(STDIN_FILENO, &c, 1) != 1)
      return res;

    if (c == 4) { // Ctrl+D
      if (res.isEmpty())
        return "\x04";
      continue;
    } else if (c == 3) { // Ctrl+C
      printf("\n");
      return "\x03";
    } else if (c == 23) { // Ctrl+W: delete word before cursor
      if (cursor > 0) {
        int i = cursor - 1;
        while (i > 0 && res[i] == ' ') i--;
        while (i > 0 && res[i] != ' ') i--;
        if (i < 0) i = 0;
        res = res.substring(0, i) + res.substring(cursor);
        cursor = i;
      }
    } else if (c == 21) { // Ctrl+U: delete line before cursor
      res = res.substring(cursor);
      cursor = 0;
    } else if (c == 11) { // Ctrl+K: delete line after cursor
      res = res.substring(0, cursor);
    } else if (c == 12) { // Ctrl+L: clear screen
      Clear();
    } else if (c == '\t') { // Tab: accept suggestion
      if (!suggestion.isEmpty()) {
        res += suggestion;
        cursor = res.length();
      }
    } else if (c == '\n' || c == '\r') {
      printf("\n");
      break;
    } else if (c == 127 || c == 8) { // Backspace
      if (cursor > 0) {
        res = res.substring(0, cursor - 1) + res.substring(cursor);
        cursor--;
      }
    } else if (c == '\x1b') {
      char seq0, seq1;
      if (read(STDIN_FILENO, &seq0, 1) == 1 &&
          read(STDIN_FILENO, &seq1, 1) == 1) {
        if (seq0 == '[') {
          if (seq1 == 'A') { // Up
            if (history && historyIdx > 0) {
              if (historyIdx == history->size())
                savedCurrent = res;
              historyIdx--;
              res = (*history)[historyIdx];
              cursor = res.length();
            }
          } else if (seq1 == 'B') { // Down
            if (history && historyIdx < history->size()) {
              historyIdx++;
              if (historyIdx == history->size())
                res = savedCurrent;
              else
                res = (*history)[historyIdx];
              cursor = res.length();
            }
          } else if (seq1 == 'C') { // Right
            if (cursor < res.length()) {
              cursor++;
            } else if (!suggestion.isEmpty()) {
              res += suggestion;
              cursor = res.length();
            }
          } else if (seq1 == 'D') { // Left
            if (cursor > 0)
              cursor--;
          } else if (seq1 == 'H') { // Home
            cursor = 0;
          } else if (seq1 == 'F') { // End
            cursor = res.length();
          } else if (seq1 >= '1' && seq1 <= '9') {
            char seq2;
            if (read(STDIN_FILENO, &seq2, 1) == 1) {
              if (seq2 == '~') {
                if (seq1 == '1' || seq1 == '7') cursor = 0; // Home
                else if (seq1 == '4' || seq1 == '8') cursor = res.length(); // End
                else if (seq1 == '3') { // Delete
                  if (cursor < res.length()) {
                    res = res.substring(0, cursor) + res.substring(cursor + 1);
                  }
                }
              } else if (seq2 == ';') {
                char seq3, seq4;
                if (read(STDIN_FILENO, &seq3, 1) == 1 && read(STDIN_FILENO, &seq4, 1) == 1) {
                  if (seq3 == '5') { // Ctrl
                    if (seq4 == 'C') { // Ctrl+Right
                      while (cursor < res.length() && res[cursor] == ' ') cursor++;
                      while (cursor < res.length() && res[cursor] != ' ') cursor++;
                    } else if (seq4 == 'D') { // Ctrl+Left
                      if (cursor > 0) {
                        cursor--;
                        while (cursor > 0 && res[cursor] == ' ') cursor--;
                        while (cursor > 0 && res[cursor - 1] != ' ') cursor--;
                      }
                    }
                  }
                }
              }
            }
          }
        } else if (seq0 == 'O') {
          if (seq1 == 'H') cursor = 0; // Home
          else if (seq1 == 'F') cursor = res.length(); // End
        }
      }
    } else if (c >= 32 && c <= 126) {
      char buf[2] = {c, '\0'};
      res = res.substring(0, cursor) + String(buf) + res.substring(cursor);
      cursor++;
    }
  }

  if (history && !res.isEmpty()) {
    history->push(res);
  }
  return res;
}

} // namespace Prompt
} // namespace Terminal
