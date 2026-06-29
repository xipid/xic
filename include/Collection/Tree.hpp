/**
 * @file Tree.hpp
 * @brief Hierarchical tree structure with selector-based querying for the Xi
 * framework.

 */

#ifndef XI_CORE_TREE_HPP
#define XI_CORE_TREE_HPP

#include "Array.hpp"
#include "String.hpp"

using namespace Xi;

namespace Collection {

class TreeItem;
class TreeBranch;

// -------------------------------------------------------------------------
// SFINAE Helpers & Type Name Extraction
// -------------------------------------------------------------------------

/**
 * @struct HasParseHydrate
 * @brief SFINAE helper to check if a class has a parseHydrate method.
 */
template <typename T> class HasParseHydrate {
private:
  typedef char YesType[1];
  typedef char NoType[2];
  template <typename C> static YesType &test(decltype(&C::parseHydrate));
  template <typename C> static NoType &test(...);

public:
  enum { value = sizeof(test<T>(0)) == sizeof(YesType) };
};

/**
 * @brief Extracts a human-readable type name from a template parameter.
 * @return Demangled type name as a String.
 */
template <typename T> String demangle_type_name() {
#if defined(__GNUC__) || defined(__clang__)
  String pretty = __PRETTY_FUNCTION__;
  long long eq = pretty.find("=");
  if (eq != -1) {
    long long bracket = pretty.indexOf(']', eq);
    if (bracket != -1) {
      String name = pretty.substring(eq + 1, bracket).trim();
      long long colon = name.indexOf("::");
      while (colon != -1) {
        name = name.substring(colon + 2);
        colon = name.indexOf("::");
      }
      return name;
    }
  }
  return "";
#elif defined(_MSC_VER)
  String sig = __FUNCSIG__;
  long long arrow = sig.indexOf('<');
  if (arrow != -1) {
    long long end = sig.indexOf('>', arrow);
    String name = sig.substring(arrow + 1, end).trim();
    long long colon = name.indexOf("::");
    while (colon != -1) {
      name = name.substring(colon + 2);
      colon = name.indexOf("::");
    }
    if (name.startsWith("class "))
      return name.substring(6);
    if (name.startsWith("struct "))
      return name.substring(7);
    return name;
  }
  return "";
#else
  return "UnknownType";
#endif
}

// -------------------------------------------------------------------------
// Selector System
// -------------------------------------------------------------------------

/**
 * @enum Combinator
 * @brief Describes the relationship between two parts of a CSS-like selector.
 */
enum class XI_EXPORT Combinator {
  NoCombinator, ///< No specific relationship.
  Descendant,   ///< Space combinator (any depth).
  Child         ///< Greater-than combinator (direct parent-child).
};

/**
 * @struct SelectorPart
 * @brief A single component of a tree selector (e.g., "tag.class1.class2").
 */
struct XI_EXPORT SelectorPart {
  String tag;            ///< Tag name (item name).
  Array<String> classes; ///< List of required classes.
  Combinator relationToLeft =
      Combinator::NoCombinator; ///< Relationship to the previous part.

  /**
   * @brief Checks if a TreeItem matches this specific selector part.
   */
  bool matches(const TreeItem *item) const;
};

// -------------------------------------------------------------------------
// TreeItem (Base Node)
// -------------------------------------------------------------------------

/**
 * @class TreeItem
 * @brief Base class for all nodes in the hierarchical tree structure.
 *
 * Provides a core set of features for hierarchical navigation, metadata
 * tagging, and advanced querying using a CSS-like selector syntax.
 */
class XI_EXPORT TreeItem {
protected:
  static Array<SelectorPart> parse_selector(const String &queryStr);
  static bool verify_chain(const TreeItem *item,
                           const Array<SelectorPart> &chain);

  template <typename... Ts>
  void query_recursive(const Array<SelectorPart> &chain,
                       Array<TreeItem *> &out);

public:
  TreeItem *parent = nullptr; ///< Parent node in the tree.

  virtual ~TreeItem() {}

  virtual String getName() const { return ""; }
  virtual bool hasClass(const char *) const { return false; }
  virtual Array<String> getClasses() const { return {}; }
  virtual TreeItem *addClass(const char *) { return this; }
  virtual void setName(const String &) {}

  /**
   * @brief Creates a deep copy of the tree item.
   * @return A pointer to the newly created clone.
   */
  virtual TreeItem *clone() const = 0;

  /**
   * @brief Dynamic type check for tree items.
   */
  template <typename T> static bool is_type(const TreeItem *item) {
    return dynamic_cast<const T *>(item) != nullptr;
  }

  /**
   * @brief Verifies if the item matches all specified types.
   */
  template <typename T, typename... Rest>
  static bool check_types(const TreeItem *item) {
    if (!is_type<T>(item))
      return false;
    if constexpr (sizeof...(Rest) > 0)
      return check_types<Rest...>(item);
    return true;
  }

  /**
   * @brief Queries the subtree for items matching the given selector.
   * @tparam Ts Optional list of types the items must match.
   * @param selector CSS-like selector string.
   * @return Array of pointers to matching items.
   */
  template <typename... Ts> Array<TreeItem *> query(const String &selector);

  /**
   * @brief Finds the first item matching the selector.
   */
  template <typename... Ts> TreeItem *find(const String &selector) {
    Array<TreeItem *> res = query<Ts...>(selector);
    return (res.length() > 0) ? res[0] : nullptr;
  }

  /**
   * @brief Flattens the entire subtree into a linear array.
   */
  Array<TreeItem *> flatten();
};

// -------------------------------------------------------------------------
// TaggedTreeItem
// -------------------------------------------------------------------------

/**
 * @class TaggedTreeItem
 * @brief A TreeItem implementation with built-in name and class storage.
 */
class XI_EXPORT TaggedTreeItem : virtual public TreeItem {
public:
  String name;           ///< Name of the item (tag).
  Array<String> classes; ///< Associated CSS-like classes.

  virtual String getName() const override { return name; }
  virtual void setName(const String &newName) override { name = newName; }
  virtual bool hasClass(const char *cls) const override {
    return classes.find(cls) != -1;
  }
  virtual Array<String> getClasses() const override { return classes; }
  virtual TreeItem *addClass(const char *cls) override {
    if (!hasClass(cls))
      classes.push(cls);
    return this;
  }

  virtual TreeItem *clone() const override {
    TaggedTreeItem *res = new TaggedTreeItem();
    res->name = name;
    res->classes = classes;
    return res;
  }
};

// -------------------------------------------------------------------------
// TreeBranch
// -------------------------------------------------------------------------

/**
 * @class TreeBranch
 * @brief A node that acts as a container for child TreeItems.
 */
class XI_EXPORT TreeBranch : virtual public TreeItem, public Array<TreeItem *> {
public:
  TreeBranch() {}
  virtual ~TreeBranch() {
    for (usz i = 0; i < size(); ++i) {
      delete (*this)[i];
    }
  }

  /**
   * @brief Adds a child to this branch.
   * @return The added child pointer.
   */
  template <typename T> T *add(T *child) {
    if (!child)
      return nullptr;
    child->parent = this;
    this->push(child);
    return child;
  }

  TreeItem *operator[](const String &key) const { return get(key); }
  using Array<TreeItem *>::operator[];

  /**
   * @brief Retrieves a child by name.
   */
  TreeItem *get(const String &key) const {
    for (usz i = 0; i < size(); ++i) {
      if ((*this)[i] && (*this)[i]->getName() == key)
        return (*this)[i];
    }
    return nullptr;
  }

  /**
   * @brief Retrieves a child by name and type.
   */
  template <typename T> T *get(const String &key) const {
    for (usz i = 0; i < size(); ++i) {
      TreeItem *child = (*this)[i];
      if (child && child->getName() == key) {
        if (T *t = dynamic_cast<T *>(child))
          return t;
      }
    }
    return nullptr;
  }

  /**
   * @brief Retrieves the first child matching the specified type.
   */
  template <typename T> T *get() const {
    for (usz i = 0; i < size(); ++i) {
      if (T *t = dynamic_cast<T *>((*this)[i]))
        return t;
    }
    return nullptr;
  }

  TreeItem *get() const { return (size() > 0) ? (*this)[0] : nullptr; }

  /**
   * @brief Removes a child from the branch without deleting it.
   */
  void removeChild(TreeItem *child) {
    long long idx = -1;
    for (usz i = 0; i < size(); ++i)
      if ((*this)[i] == child) {
        idx = i;
        break;
      }
    if (idx != -1) {
      this->splice(idx, 1);
      child->parent = nullptr;
    }
  }

  virtual TreeItem *clone() const override {
    TreeBranch *res = new TreeBranch();
    for (usz i = 0; i < size(); ++i) {
      if ((*this)[i])
        res->add((*this)[i]->clone());
    }
    return res;
  }
};

/**
 * @class TaggedTreeBranch
 * @brief A container node with its own name and classes.
 */
class XI_EXPORT TaggedTreeBranch : public TreeBranch, public TaggedTreeItem {
public:
  TaggedTreeBranch() {}
  virtual ~TaggedTreeBranch() {}

  virtual TreeItem *clone() const override {
    TaggedTreeBranch *res = new TaggedTreeBranch();
    res->name = name;
    res->classes = classes;
    for (usz i = 0; i < size(); ++i) {
      if ((*this)[i])
        res->add((*this)[i]->clone());
    }
    return res;
  }
};

// -------------------------------------------------------------------------
// Specialized Branches and Items
// -------------------------------------------------------------------------

template <typename T> class XI_EXPORT TreeArrayBranch : public TreeBranch {
public:
  virtual ~TreeArrayBranch() {}

  virtual TreeItem *clone() const override {
    TreeArrayBranch<T> *res = new TreeArrayBranch<T>();
    for (usz i = 0; i < this->size(); ++i) {
      if ((*this)[i])
        res->add((*this)[i]->clone());
    }
    return res;
  }
};

template <typename T>
class XI_EXPORT TaggedTreeArrayBranch : public TreeArrayBranch<T>,
                                        public TaggedTreeItem {
public:
  virtual ~TaggedTreeArrayBranch() {}

  virtual TreeItem *clone() const override {
    TaggedTreeArrayBranch<T> *res = new TaggedTreeArrayBranch<T>();
    res->name = name;
    res->classes = classes;
    for (usz i = 0; i < this->size(); ++i) {
      if ((*this)[i])
        res->add((*this)[i]->clone());
    }
    return res;
  }
};

/**
 * @class TreeItemT
 * @brief A generic leaf node carrying a specific value.
 */
template <typename T> class XI_EXPORT TreeItemT : virtual public TreeItem {
public:
  T value;
  TreeItemT() {}
  TreeItemT(const T &v) : value(v) {}
  virtual ~TreeItemT() {}

  virtual TreeItem *clone() const override { return new TreeItemT<T>(value); }
};



/**
 * @class TaggedTreeItemT
 * @brief A leaf node with metadata and a value.
 */
template <typename T>
class XI_EXPORT TaggedTreeItemT : public TreeItemT<T>, public TaggedTreeItem {
public:
  TaggedTreeItemT() {}
  TaggedTreeItemT(const T &v) : TreeItemT<T>(v) {}
  virtual ~TaggedTreeItemT() {}

  virtual TreeItem *clone() const override {
    TaggedTreeItemT<T> *res = new TaggedTreeItemT<T>(this->value);
    res->name = name;
    res->classes = classes;
    return res;
  }
};

// -------------------------------------------------------------------------
// Implementations
// -------------------------------------------------------------------------

inline Array<SelectorPart> TreeItem::parse_selector(const String &queryStr) {
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
    Array<String> sub = t.split(".");
    current.tag = (t.c_str()[0] != '.') ? sub[0] : "";
    current.classes.clear();
    for (usz k = 1; k < sub.length(); ++k)
      if (sub[k].length() > 0)
        current.classes.push(sub[k]);
    current.relationToLeft = pendingComb;
    parts.push(Xi::Move(current));
    pendingComb = Combinator::Descendant;
  }
  return parts;
}

inline bool TreeItem::verify_chain(const TreeItem *item,
                                   const Array<SelectorPart> &chain) {
  if (chain.length() == 0)
    return true;
  long long idx = chain.length() - 1;
  if (!chain[idx].matches(item))
    return false;

  const TreeItem *curr = item;
  while (idx > 0 && curr) {
    const SelectorPart &part = chain[idx];
    const SelectorPart &prev = chain[idx - 1];

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

template <typename... Ts>
void TreeItem::query_recursive(const Array<SelectorPart> &chain,
                               Array<TreeItem *> &out) {
  bool typeMatch = true;
  if constexpr (sizeof...(Ts) > 0) {
    typeMatch = TreeItem::check_types<Ts...>(this);
  }
  if (typeMatch && (chain.length() == 0 || verify_chain(this, chain)))
    out.push(this);
  if (TreeBranch *branch = dynamic_cast<TreeBranch *>(this)) {
    for (usz i = 0; i < branch->size(); ++i) {
      if (TreeItem *child = (*branch)[i])
        child->query_recursive<Ts...>(chain, out);
    }
  }
}

template <typename... Ts>
Array<TreeItem *> TreeItem::query(const String &selector) {
  Array<TreeItem *> results;
  Array<SelectorPart> chain = parse_selector(selector);
  if (TreeBranch *branch = dynamic_cast<TreeBranch *>(this)) {
    for (usz i = 0; i < branch->size(); ++i) {
      if ((*branch)[i])
        (*branch)[i]->query_recursive<Ts...>(chain, results);
    }
  }
  return results;
}

inline Array<TreeItem *> TreeItem::flatten() {
  Array<TreeItem *> out;
  Array<SelectorPart> empty;
  if (TreeBranch *branch = dynamic_cast<TreeBranch *>(this)) {
    for (usz i = 0; i < branch->size(); ++i)
      if ((*branch)[i])
        (*branch)[i]->query_recursive<>(empty, out);
  }
  return out;
}

inline bool SelectorPart::matches(const TreeItem *item) const {
  if (tag.length() > 0 && tag != "*" && item->getName() != tag)
    return false;
  for (usz i = 0; i < classes.length(); ++i)
    if (!item->hasClass(classes[i].c_str()))
      return false;
  return true;
}

} // namespace Collection

#endif // XI_CORE_TREE_HPP