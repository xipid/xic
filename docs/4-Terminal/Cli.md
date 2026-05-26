# Command Line Interface (CLI)

The `Command` class provides a fluid, chainable, and zero-boilerplate API for parsing command-line arguments. It manages sub-commands, option flags, data types, and help generation.

---

## Fluid Chaining

Xi's parser is designed to be readable and compact. Every configuration method returns a reference to the `Command` object for immediate chaining.

```cpp
#include <Terminal/Command.hpp>
using namespace Terminal;

int main(int argc, char** argv) {
  Command args(argc, argv);
  
  args.description("Xi Compiler")
      .version("1.2.0")
      .usage("xic [command] [options]");
}
```

---

## Options & Flags

Options can have multiple aliases (short or long). The parser handles clusters (`-abc`), values (`--flag=value`), and negation.

```cpp
// Check for a boolean flag
if (args.option("-v --verbose").description("Enable debug logs")) {
  // logic...
}

// Retrieve a string option with defaults and environment fallback
String out = args.option("-o --output")
                 .description("Output binary path")
                 .defaults("dist/app.bin")
                 .env("XI_OUT")
                 .string();
```

### Advanced Parsing Features
- **Negation**: Use `--!flag`, `-!f`, or `--no-flag` to explicitly set an option to `false`.
- **CSV Values**: Options like `--input=a,b,c` are automatically parsed into the `.value` array.

---

## Sub-Commands

Commands can be nested infinitely. A sub-command is only "active" if it matches the parent's primary positional argument.

```cpp
Command& build = args.command("build b").description("Build the project");

if (build) {
  bool release = build.option("--release").boolean();
  // ... run build logic ...
}
```

---

## Automation & UX

### 1. Typo Suggestions
If a user provides an unknown option, `Command` can suggest the closest match using Levenshtein distance.

```cpp
Array<String> unknown = args.options(); // Get unclaimed flags
for (auto &opt : unknown) {
  String suggestion = args.suggest(opt);
  if (suggestion) log("Did you mean " + suggestion + "?");
}
```

### 2. Auto Help Generation
The framework automatically generates a formatted help screen based on your registered commands and descriptions.

```cpp
if (args.option("-h --help")) {
  log(args.help());
}
```

### 3. Shell Completion
 Xi can generate its own completion scripts for Bash, Zsh, and Fish.

```cpp
if (args.option("--completion")) {
  args.generateCompletion("zsh", "xic");
}
```

---

> [!TIP]
> Use `.integer()`, `.number()`, or `.boolean()` to immediately cast option values to their respective types.
