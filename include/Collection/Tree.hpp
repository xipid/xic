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

class NodeBase;

// -------------------------------------------------------------------------
// SFINAE Helpers & Type Name Extraction
// -------------------------------------------------------------------------

template <typename T> class HasParseHydrate {
private:
  typedef char YesType[1];
  typedef char NoType[2];
  template <typename C> static YesType &test(decltype(&C::parseHydrate));
  template <typename C> static NoType &test(...);

public:
  enum { value = sizeof(test<T>(0)) == sizeof(YesType) };
};

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

enum class XI_EXPORT Combinator {
  NoCombinator,
  Descendant,
  Child
};

struct XI_EXPORT SelectorPart {
  String tag;
  Array<String> classes;
  Combinator relationToLeft = Combinator::NoCombinator;

  bool matches(const NodeBase *item) const;
};

// -------------------------------------------------------------------------
// NodeBase (Root of hierarchy)
// -------------------------------------------------------------------------

class XI_EXPORT NodeBase : public Array<NodeBase *> {
protected:
  static Array<SelectorPart> parse_selector(const String &queryStr);
  static bool verify_chain(const NodeBase *item, const Array<SelectorPart> &chain);

  template <typename... Ts>
  void query_recursive(const Array<SelectorPart> &chain, Array<NodeBase *> &out);

public:
  NodeBase *parent = nullptr;

  virtual ~NodeBase() {
    for (usz i = 0; i < size(); ++i) {
      delete (*this)[i];
    }
  }

  virtual String getName() const { return ""; }
  virtual bool hasClass(const char *) const { return false; }
  virtual Array<String> getClasses() const { return {}; }
  virtual NodeBase *addClass(const char *) { return this; }
  virtual void setName(const String &) {}
  virtual bool isContainer() const { return true; }

  virtual NodeBase *clone() const {
    NodeBase *res = new NodeBase();
    for (usz i = 0; i < size(); ++i) {
      if ((*this)[i]) res->add((*this)[i]->clone());
    }
    return res;
  }

  template <typename T> T *add(T *child) {
    if (!child) return nullptr;
    child->parent = this;
    this->push(child);
    return child;
  }

  NodeBase *operator[](const String &key) const { return get(key); }
  using Array<NodeBase *>::operator[];

  NodeBase *get(const String &key) const {
    for (usz i = 0; i < size(); ++i) {
      if ((*this)[i] && (*this)[i]->getName() == key)
        return (*this)[i];
    }
    return nullptr;
  }

  template <typename T> T *get(const String &key) const {
    for (usz i = 0; i < size(); ++i) {
      NodeBase *child = (*this)[i];
      if (child && child->getName() == key) {
        if (T *t = dynamic_cast<T *>(child))
          return t;
      }
    }
    return nullptr;
  }

  template <typename T> T *get() const {
    for (usz i = 0; i < size(); ++i) {
      if (T *t = dynamic_cast<T *>((*this)[i]))
        return t;
    }
    return nullptr;
  }

  NodeBase *get() const { return (size() > 0) ? (*this)[0] : nullptr; }

  void removeChild(NodeBase *child) {
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

  template <typename T> static bool is_type(const NodeBase *item) {
    return dynamic_cast<const T *>(item) != nullptr;
  }

  template <typename T, typename... Rest>
  static bool check_types(const NodeBase *item) {
    if (!is_type<T>(item)) return false;
    if constexpr (sizeof...(Rest) > 0) return check_types<Rest...>(item);
    return true;
  }

  template <typename... Ts> Array<NodeBase *> query(const String &selector);

  template <typename... Ts> NodeBase *find(const String &selector) {
    Array<NodeBase *> res = query<Ts...>(selector);
    return (res.length() > 0) ? res[0] : nullptr;
  }

  Array<NodeBase *> flatten();
};

// -------------------------------------------------------------------------
// Node<T = void> and NamedNode<T = void>
// -------------------------------------------------------------------------

template <typename T = void>
class Node;

template <>
class XI_EXPORT Node<void> : virtual public NodeBase {
public:
  using NodeBase::NodeBase;
};

template <typename T>
class XI_EXPORT Node : virtual public Node<void> {
public:
  T value;
  Node() : Node<void>() {}
  Node(const T &v) : Node<void>(), value(v) {}
  virtual ~Node() override = default;

  virtual bool isContainer() const override { return false; }

  virtual NodeBase *clone() const override { 
    Node<T>* res = new Node<T>(value);
    for (usz i = 0; i < size(); ++i) {
      if ((*this)[i]) res->add((*this)[i]->clone());
    }
    return res;
  }
};

template <typename T = void>
class NamedNode;

template <>
class XI_EXPORT NamedNode<void> : virtual public Node<void> {
public:
  String name;
  Array<String> classes;

  virtual String getName() const override { return name; }
  virtual void setName(const String &newName) override { name = newName; }
  virtual bool hasClass(const char *cls) const override {
    return classes.find(cls) != -1;
  }
  virtual Array<String> getClasses() const override { return classes; }
  virtual NodeBase *addClass(const char *cls) override {
    if (!hasClass(cls)) classes.push(cls);
    return this;
  }

  virtual NodeBase *clone() const override {
    NamedNode<void> *res = new NamedNode<void>();
    res->name = name;
    res->classes = classes;
    for (usz i = 0; i < size(); ++i) {
      if ((*this)[i]) res->add((*this)[i]->clone());
    }
    return res;
  }
};

template <typename T>
class XI_EXPORT NamedNode : virtual public Node<T>, virtual public NamedNode<void> {
public:
  NamedNode() : Node<T>(), NamedNode<void>() {}
  NamedNode(const T &v) : Node<T>(v), NamedNode<void>() {}
  virtual ~NamedNode() override = default;

  virtual NodeBase *clone() const override {
    NamedNode<T> *res = new NamedNode<T>(this->value);
    res->name = this->name;
    res->classes = this->classes;
    for (usz i = 0; i < size(); ++i) {
      if ((*this)[i]) res->add((*this)[i]->clone());
    }
    return res;
  }
};

// -------------------------------------------------------------------------
// Backward compat typedefs
// -------------------------------------------------------------------------
using TreeItem = NodeBase;
using TreeBranch = NodeBase;
using TaggedTreeItem = NamedNode<void>;
using TaggedTreeBranch = NamedNode<void>;
template<typename T> using TreeArrayBranch = NodeBase;
template<typename T> using TaggedTreeArrayBranch = NamedNode<void>;
template<typename T> using TreeItemT = Node<T>;
template<typename T> using TaggedTreeItemT = NamedNode<T>;

// -------------------------------------------------------------------------
// Implementations
// -------------------------------------------------------------------------

inline Array<SelectorPart> NodeBase::parse_selector(const String &queryStr) {
  Array<SelectorPart> parts;
  if (queryStr.length() == 0) return parts;
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

inline bool NodeBase::verify_chain(const NodeBase *item, const Array<SelectorPart> &chain) {
  if (chain.length() == 0) return true;
  long long idx = chain.length() - 1;
  if (!chain[idx].matches(item)) return false;

  const NodeBase *curr = item;
  while (idx > 0 && curr) {
    const SelectorPart &part = chain[idx];
    const SelectorPart &prev = chain[idx - 1];

    if (part.relationToLeft == Combinator::Child) {
      curr = curr->parent;
      if (!curr || !prev.matches(curr)) return false;
    } else if (part.relationToLeft == Combinator::Descendant) {
      bool found = false;
      while (curr->parent) {
        curr = curr->parent;
        if (prev.matches(curr)) {
          found = true;
          break;
        }
      }
      if (!found) return false;
    }
    idx--;
  }
  return (idx == 0);
}

template <typename... Ts>
void NodeBase::query_recursive(const Array<SelectorPart> &chain, Array<NodeBase *> &out) {
  bool typeMatch = true;
  if constexpr (sizeof...(Ts) > 0) {
    typeMatch = NodeBase::check_types<Ts...>(this);
  }
  if (typeMatch && (chain.length() == 0 || verify_chain(this, chain)))
    out.push(this);
  for (usz i = 0; i < size(); ++i) {
    if (NodeBase *child = (*this)[i])
      child->query_recursive<Ts...>(chain, out);
  }
}

template <typename... Ts>
Array<NodeBase *> NodeBase::query(const String &selector) {
  Array<NodeBase *> results;
  Array<SelectorPart> chain = parse_selector(selector);
  this->query_recursive<Ts...>(chain, results);
  return results;
}

inline Array<NodeBase *> NodeBase::flatten() {
  Array<NodeBase *> out;
  Array<SelectorPart> empty;
  for (usz i = 0; i < size(); ++i) {
    if ((*this)[i])
      (*this)[i]->query_recursive<>(empty, out);
  }
  return out;
}

inline bool SelectorPart::matches(const NodeBase *item) const {
  if (tag.length() > 0 && tag != "*" && item->getName() != tag) return false;
  for (usz i = 0; i < classes.length(); ++i)
    if (!item->hasClass(classes[i].c_str())) return false;
  return true;
}

// Global Free Functions
template <typename... Ts>
Array<NodeBase *> queryAll(NodeBase *root, const String &selector) {
  if (!root) return {};
  return root->query<Ts...>(selector);
}

template <typename... Ts>
NodeBase *query(NodeBase *root, const String &selector) {
  if (!root) return nullptr;
  return root->find<Ts...>(selector);
}

} // namespace Collection

#endif // XI_CORE_TREE_HPP