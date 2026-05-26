# Hierarchical Trees

The **Collection::Tree** module is the data backbone of the xic framework. It provides a polymorphic, hierarchical structure with a CSS-inspired selector system for querying.

---

## Core Classes

### TreeItem
The base node. Every element in a tree—whether it's a value, a branch, or a configuration block—inherits from `TreeItem`.

### TreeBranch
A container node that can hold multiple child `TreeItems`. It acts as a directory or an object in a JSON-like structure.

### TreeItemT<T>
A "leaf" node that carries a specific value (e.g., `int`, `String`, `Matrix4`).

---

## Selector Querying

The most powerful feature of xic Trees is the ability to query the hierarchy using CSS-like string selectors.

-   **`tag`**: Matches nodes by name.
-   **`.class`**: Matches nodes with a specific CSS-like class.
-   **`>`**: Direct child combinator.
-   **` `**: Descendant combinator (default).

```cpp
// Initialize a branch
TreeBranch *root = ...;

// Query for all items with class "active" inside "config"
Array<TreeItem *> results = root->query("config .active");

// Find the first value of type float named "speed"
TreeItemT<f32> *speed = root->find<TreeItemT<f32>>("settings > speed");
```

---

## Dynamic Type Injection

xic supports SFINAE-based "hydration." If a class implements a `parseHydrate` method, the tree system can automatically populate the object from a tree structure (e.g., loaded from YAML).

### Type Detection
The system uses template-based demangling to identify types at runtime without requiring full RTTI (on supported compilers like GCC/Clang).

```cpp
if (TreeItem::is_type<TaggedTreeBranch>(node)) {
    // Handle tagged branch
}
```

---

## Operations

### Cloning
Trees support deep cloning, which is essential for working with configuration templates.

```cpp
TreeItem *template = ...;
TreeItem *instance = template->clone();
```

### Flattening
You can flatten a complex hierarchical subtree into a linear `Array` for bulk processing.

```cpp
Array<TreeItem *> allNodes = root->flatten();
```

---

## Best Practices

1.  **Prefer Classes Over Names**: Tagging nodes with classes (e.g., `.hidden`, `.system`) makes your queries more robust than relying on unique names.
2.  **Use `find<T>` for Type Safety**: When querying, always specify the expected node type to avoid manual dynamic casting.
3.  **Parentage**: Always be aware that adding a child to a branch automatically sets its `parent` pointer. Circular references are not supported and will lead to infinite recursion during deep clones.
