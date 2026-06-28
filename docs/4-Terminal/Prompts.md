# Interactive Prompts

The `Terminal::Prompt` namespace provides a suite of interactive, rich-text console interfaces for acquiring input from users. These utilities natively utilize `RawMode` terminal polling, allowing them to cleanly bypass standard buffer limitations and handle advanced ANSI VT100 control sequences for features like history navigation and inline typing edits.

---

## Single Input Selection

### 1. Simple Line Input
`Terminal::Prompt::readLine()` is a fully native implementation of a terminal line editor (comparable to `readline` or `linenoise`). It supports **up/down arrow history cycling**, **left/right arrow inline cursor edits**, and gracefully degrades when piped to non-TTY scripts.

```cpp
#include <Terminal/Prompt.hpp>
using namespace Terminal;

Array<String> commandHistory;

while (true) {
  // Arrow keys can be used here to cycle through commandHistory!
  String input = Prompt::readLine("> ", &commandHistory);

  if (input == "\x04" || input == "\x03") { // Ctrl+D or Ctrl+C
    break;
  }
}
```

### 2. Asking with Defaults
```cpp
// Returns "Y" if the user presses enter.
String choice = Prompt::ask("Continue?", "Y");
```

### 3. Password Input
Securely reads keystrokes without echoing them back to the terminal.
```cpp
String pass = Prompt::password("Enter Database Password");
```

---

## Interactive Menus

### 1. Single Selection List
Renders an interactive menu. The user navigates up and down using arrow keys and presses `Enter` to confirm.
```cpp
Array<String> options = {"Deploy to Staging", "Run Tests", "Abort"};
usz selectionIndex = Prompt::select("What would you like to do?", options);

log("You chose: " + options[selectionIndex]);
```

### 2. Multi-Selection List
Renders a checklist where users use the `Spacebar` to toggle options `[x]` and `Enter` to confirm their submission.
```cpp
Array<String> modules = {"Frontend", "Backend", "Database", "Cache"};
Array<usz> selectedIndices = Prompt::multiSelect("Select modules to build:", modules);
```

> [!TIP]
> All `Terminal::Prompt` interfaces automatically detect if `stdin` is not a TTY (for example, if a user is piping a script `< <(echo ...)`). In these headless environments, the prompts will safely disable ANSI escape sequences and gracefully fall back to standard `fgets` stream ingestion.
