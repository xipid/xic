# YAML and JSON Processing

The **Encoding::YAML** module provides a high-performance, unified parser for both YAML and JSON. It is designed to bridge the gap between human-readable configuration files and the xic **Tree** collection system.

---

## Unified Parser

One of xic's core strengths is its "context-aware" parser. You don't need a separate library for JSON; the YAML engine handles standard JSON flow styles, anchors, aliases, and multiple comment formats seamlessly.

### Supported Features
-   **YAML 1.2 Syntax**: Standard block and flow styles.
-   **JSON Interop**: Valid JSON is valid YAML; the parser handles both automatically.
-   **Mixed Comments**: Supports `#`, `//`, and `/* ... */` comments, even in JSON streams.
-   **Object Hydration**: Automatic instantiation of custom C++ classes based on `_type` tags.

---

## Basic Usage

The simplest way to use the module is through the `parseYAML` and `toYAML` helpers.

```cpp
using namespace Collection;
using namespace Encoding;

TaggedTreeBranch root;
String input = "name: 'System'\nversion: 1.0\n# User config\nuser:\n  id: 42";

if (parseYAML(input, root)) {
    String name = root["name"].toString();
    int userId = root.find<TreeItemT<int>>("user > id")->value;
}
```

---

## Object Hydration

Hydration allows you to turn a generic structural tree into a typed C++ object. If a branch contains a `_type` entry matching your class name, xic can automatically instantiate it.

```yaml
# config.yaml
server:
  _type: "NetworkServer"
  port: 8080
  protocol: "HTTP"
```

```cpp
// This will parse the YAML and automatically create an instance 
// of 'NetworkServer' if registered.
NetworkServer root;
parseYAML<NetworkServer>(fileContent, root);
```

---

## Serialization

You can serialize any `TreeItem` back into a human-readable string.

```cpp
// To YAML (with 2-space indentation)
String yamlText = toYAML(root, 2);

// To JSON (with 4-space indentation)
String jsonText = toJSON(root, 4);
```

---

## Best Practices

1.  **Prefer YAML for Config**: YAML's support for comments and cleaner syntax is superior to JSON for human-edited configuration files.
2.  **Use Anchors for DRY**: Take advantage of YAML anchors (`&`) and aliases (`*`) to reduce repetition in large configuration trees.
3.  **Indentation Consistency**: While the parser is robust, sticking to a consistent indentation (usually 2 or 4 spaces) ensures maximum compatibility with other YAML tools.
