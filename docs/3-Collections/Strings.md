# Dynamic Strings

The **Collection::String** class is the primary text manipulation container for the xic framework. It is designed to be a high-performance, Copy-on-Write (COW) optimized alternative to `std::string`.

---

## Core Features

-   **Copy-on-Write (COW)**: Copying a string is essentially free (only increments a reference count). Memory is only duplicated when one of the instances is modified.
-   **SSO/COW Hybrid**: Inherits from `InlineArray`, leveraging internal buffers for small strings to avoid heap allocation.
-   **Fluent API**: Mimics modern high-level languages like JavaScript for ease of use.
-   **No Exceptions**: All methods are designed to handle errors gracefully through default return values or status checks.

---

## Basic Manipulation

```cpp
String s = "  Hello xic World  ";

// Fluent cleaning
String clean = s.trim().toLowerCase(); // "hello xic world"

// Querying
if (clean.startsWith("hello")) {
    long long idx = clean.indexOf("xic");
}

// Transformation
String replaced = clean.replace("world", "embedded");
```

---

## Numeric Conversion

xic strings include built-in parsers and formatting constructors:

```cpp
// String to Numbers
int i = String("42").toInt();
f64 d = String("3.14").toDouble();

// Numbers to String
String s1(100);    // "100"
String s2(3.1415); // "3.1415"
```

---

## Binary and Serialization

The String class is also used as a byte buffer for binary protocols.

### Variable-Length Encoding (VarLong)
Essential for efficient networking, VarLongs encode large integers into fewer bytes using the LSB of each byte as a continuation flag.

```cpp
String buffer;
buffer.pushVarLong(123456789ULL); // Encodes into ~4-5 bytes

long long val = buffer.shiftVarLong(); // Decodes and removes from start
```

### Constant-Time Comparison
For cryptographic keys or passwords, use `constantTimeEquals` to prevent timing attacks.

```cpp
String key = "...";
String input = "...";
if (key.constantTimeEquals(input)) {
    // Authorized
}
```

---

## Best Practices

1.  **Use `c_str()` with Care**: Accessing the underlying C-string pointer may trigger a null-terminator insertion and potentially a reallocation.
2.  **Avoid Raw Buffer Access**: Use `shift` and `push` methods for serializing data—they handle index management and COW automatically.
3.  **Ref Counting**: Since strings are reference-counted, passing them by value is efficient.
