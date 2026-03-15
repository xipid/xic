#ifndef XI_TREE_HPP
#define XI_TREE_HPP

#include "Array.hpp"
#include "String.hpp"

// RTTI required for dynamic_cast and typeid (Auto-Naming)
#include <typeinfo>

namespace Xi {
class TreeItem;

// -------------------------------------------------------------------------
// Selector System
// -------------------------------------------------------------------------

enum class XI_EXPORT Combinator {
  NoCombinator, // The target itself
  Descendant,   // " " (Space)
  Child         // ">"
};

struct XI_EXPORT SelectorPart {
  String tag;
  Array<String> classes;
  Combinator relationToLeft = Combinator::NoCombinator;

  bool matches(const TreeItem *item) const;
};

// -------------------------------------------------------------------------
// TreeItem
// -------------------------------------------------------------------------
class XI_EXPORT TreeItem {
protected:
  // ---------------------------------------------------------------------
  // Parsing & Verification Helpers
  // ---------------------------------------------------------------------

  static Array<SelectorPart> parse_selector(const String &queryStr) {
    Array<SelectorPart> parts;
    if (queryStr.length() == 0)
      return parts;

    Array<String> tokens = queryStr.split(" ");

    SelectorPart current;
    Combinator pendingComb = Combinator::Descendant;

    for (usz i = 0; i < tokens.length(); ++i) {
      String t = tokens[i];
      if (t == ">") {
        pendingComb = Combinator::Child;
        continue;
      }

      // Parse "TagName.class1.class2"
      Array<String> sub = t.split(".");

      // If "Tag.cls", sub[0]="Tag". If ".cls", sub[0]="".
      if (t.c_str()[0] != '.') {
        current.tag = sub[0];
      } else {
        current.tag = "";
      }

      current.classes.clear();
      for (usz k = 1; k < sub.length(); ++k) {
        if (sub[k].length() > 0)
          current.classes.push(sub[k]);
      }

      current.relationToLeft = pendingComb;
      parts.push(Xi::Move(current));
      pendingComb = Combinator::Descendant;
    }
    return parts;
  }

  // Right-to-Left Matching Strategy (CSS Engine style)
  // Verifies if 'item' satisfies the last part of 'chain',
  // and if its ancestors satisfy the rest.
  static bool verify_chain(const TreeItem *item,
                           const Array<SelectorPart> &chain) {
    if (chain.length() == 0)
      return true;

    // 1. Check Target (Right-most selector part)
    long long idx = chain.length() - 1;
    if (!chain[idx].matches(item))
      return false;

    // 2. Walk up tree to satisfy ancestors (Left parts)
    const TreeItem *curr = item;
    while (idx > 0 && curr) {
      const SelectorPart &part = chain[idx];     // Current node match
      const SelectorPart &prev = chain[idx - 1]; // Required ancestor/parent

      if (part.relationToLeft == Combinator::Child) {
        curr = curr->parent;
        if (!curr || !prev.matches(curr))
          return false;
      } else if (part.relationToLeft == Combinator::Descendant) {
        bool found = false;
        while (curr->parent) {
          curr = curr->parent;
          if (prev.matches(curr)) {
            found = true;
            break;
          }
        }
        if (!found)
          return false;
      }
      idx--;
    }
    return (idx == 0);
  }

  // Recursive Visitor
  // Compiles type checks via templates, performs selector check at runtime.
  template <typename... Ts>
  void query_recursive(const Array<SelectorPart> &chain,
                       Array<TreeItem *> &out) {
    // 1. C++ Type Filter (Compile Time Optimized)
    // if constexpr prevents generating check_types code if Ts is empty
    bool typeMatch = true;
    if constexpr (sizeof...(Ts) > 0) {
      typeMatch = check_types<Ts...>(this);
    }

    // 2. Selector Filter
    if (typeMatch) {
      // If chain is empty (e.g. flatten or just type query), match everything
      if (chain.length() == 0 || verify_chain(this, chain)) {
        out.push(this);
      }
    }

    // 3. Recurse
    for (usz i = 0; i < children.length(); ++i) {
      children[i]->query_recursive<Ts...>(chain, out);
    }
  }

  // Helper to strip "class " or number prefixes from typeid names
  static String demangle_name(const char *raw) {
    String s;
    // Skip leading digits (Itanium ABI length prefixes)
    while (*raw >= '0' && *raw <= '9')
      raw++;
    // Skip "class " (MSVC sometimes)
    if (raw[0] == 'c' && raw[1] == 'l' && raw[2] == 'a' && raw[3] == 's' &&
        raw[4] == 's' && raw[5] == ' ')
      raw += 6;
    s = raw;
    return s;
  }

public:
  String name;
  Array<String> classes;
  TreeItem *parent = null;
  Array<TreeItem *> children;

  TreeItem() {}

  virtual ~TreeItem() {
    for (usz i = 0; i < children.length(); ++i)
      delete children[i];
  }

  // ---------------------------------------------------------------------
  // Management
  // ---------------------------------------------------------------------

  template <typename T> T *add(T *child) {
    if (!child)
      return null;

    // 1. Auto-Naming:
    // If child has no name set manually, use its C++ class name.
    if (child->name.length() == 0) {
      child->name = demangle_name(typeid(*child).name());
    }

    // 2. Link
    child->parent = this;
    children.push(child);
    return child;
  }

  bool hasClass(const char *cls) const { return classes.find(cls) != -1; }

  TreeItem *addClass(const char *cls) {
    if (!hasClass(cls))
      classes.push(cls);
    return this;
  }

  // ---------------------------------------------------------------------
  // Query / Find
  // ---------------------------------------------------------------------

  template <typename T> static bool is_type(const TreeItem *item) {
    return dynamic_cast<const T *>(item) != null;
  }

  // Variadic Template Type Checker
  template <typename T, typename... Rest>
  static bool check_types(const TreeItem *item) {
    if (!is_type<T>(item))
      return false;
    if constexpr (sizeof...(Rest) > 0) {
      return check_types<Rest...>(item);
    }
    return true;
  }

  // Main Query Function
  // query("Tag") | query(".class") | query<Type>("") | query<Type>(".class")
  template <typename... Ts> Array<TreeItem *> query(const String &selector) {
    Array<TreeItem *> results;
    Array<SelectorPart> chain = parse_selector(selector);

    // Recursively search children (excluding self from result)
    for (usz i = 0; i < children.length(); ++i) {
      children[i]->query_recursive<Ts...>(chain, results);
    }

    return results;
  }

  template <typename... Ts> TreeItem *find(const String &selector) {
    Array<TreeItem *> res = query<Ts...>(selector);
    if (res.length() > 0)
      return res[0];
    return null;
  }

  // Flatten: Get all descendants
  Array<TreeItem *> flatten() {
    Array<TreeItem *> out;
    Array<SelectorPart> empty;
    for (usz i = 0; i < children.length(); ++i) {
      children[i]->query_recursive<>(empty, out);
    }
    return out;
  }
};

// -------------------------------------------------------------------------
// Implementation
// -------------------------------------------------------------------------

inline bool SelectorPart::matches(const TreeItem *item) const {
  // Tag Match (Case sensitive Name)
  if (tag.length() > 0 && tag != "*") {
    if (item->name != tag)
      return false;
  }
  // Class Match (All required classes must exist)
  for (usz i = 0; i < classes.length(); ++i) {
    if (!item->hasClass(classes[i].c_str()))
      return false;
  }
  return true;
}
} // namespace Xi

#endif // XI_TREE_HPP