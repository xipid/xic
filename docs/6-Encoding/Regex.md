# Regular Expressions

The **Encoding::Regex** module provides a powerful, DFA/VM hybrid engine for string pattern matching. It is designed to be lean enough for embedded systems while supporting modern features like named capture groups and lookaround assertions.

---

## Pattern Matching

The `Regex` class compiles your pattern into a bytecode that is executed by a specialized virtual machine.

```cpp
using namespace Encoding;

Regex emailPattern("^[a-zA-Z0-9._%+-]+@[a-zA-Z0-9.-]+\\.[a-zA-Z]{2,}$");

if (emailPattern.matchAll("test@example.com").length() > 0) {
    // Valid email
}
```

---

## Matches and Capture Groups

When a match is found, a `RegexMatch` object provides access to the full matched string and any capture groups.

```cpp
Regex userReg("user: (?<name>\\w+)");
Array<RegexMatch> matches = userReg.matchAll("user: alice, user: bob");

for (auto &m : matches) {
    String name = m.namedGroups["name"];
    printf("Found user: %s\n", name.c_str());
}
```

---

## Common Utilities

The module also integrates with the `String` class to provide familiar text processing utilities.

### Splitting
```cpp
Regex csv(",\\s*");
Array<String> parts = csv.split("one, two,   three");
```

### Replacement
```cpp
Regex word("badword");
String clean = word.replace("This is a badword", "****");
```

---

## Engine Features

-   **Named Capture Groups**: Use `(?<name>...)` for readable results.
-   **Lookaround Assertions**: Supports both positive and negative lookahead (`(?=...)`, `(?!...)`) and lookbehind (`(?<=...)`, `(?<!...)`).
-   **Performance**: Uses a skip-table for literal prefixes and a DFA cache for repeating patterns to ensure high-speed matching.
-   **Safety**: Includes a recursion limit and an optional time limit (`limitUs`) to prevent "catastrophic backtracking" in potentially malicious patterns.

---

## Best Practices

1.  **Pre-compile Patterns**: `Regex` compilation is expensive. Create your `Regex` objects once (e.g., as static members) and reuse them for matching.
2.  **Avoid Excessive Backtracking**: Be careful with "catastrophic" patterns like `(a+)+`. Use non-greedy quantifiers (`*?`, `+?`) where appropriate.
3.  **Use Raw Strings**: When writing patterns in C++, use raw string literals `R"(...)"` to avoid double-escaping backslashes.
    ```cpp
    Regex r(R"(\d+\.\d+)"); // Matches floats
    ```
