#include "../../include/Encoding/Yaml.hpp"
#include "../../include/Collection/Map.hpp"
#include "../../include/Xi/Primitives.hpp"

namespace Encoding {

using namespace Xi;
using namespace Collection;

/**
 * @brief Checks if a character is a whitespace character.
 */
static inline bool isSpace(char c) {
  return c == ' ' || c == '\t' || c == '\n' || c == '\r';
}

/**
 * @brief Adds indentation to a string.
 */
static inline void emitIdent(String &str, int count) {
  for (int i = 0; i < count; i++)
    str += " ";
}

/**
 * @struct YamlParser
 * @brief Internal state-machine based parser for unified YAML and JSON.
 */
struct YamlParser {
  const String &input;
  usz i = 0;
  int currentIndent = 0;
  bool isNewLine = true;
  Map<String, TreeItem *> anchors;

  YamlParser(const String &s) : input(s) {}

  void skipSpace() {
    while (i < input.length()) {
      char c = input.charAt(i);
      if (c == ' ') {
        if (isNewLine)
          currentIndent++;
        i++;
      } else if (c == '\t') {
        if (isNewLine)
          currentIndent += 4; // Standard tab-to-space
        i++;
      } else if (c == '\r' || c == '\n') {
        isNewLine = true;
        currentIndent = 0;
        i++;
      } else {
        isNewLine = false;
        break;
      }
    }
  }

  void skipComments(TreeBranch *node = nullptr) {
    while (i < input.length()) {
      skipSpace();
      if (i + 1 < input.length()) {
        char c1 = input.charAt(i);
        char c2 = input.charAt(i + 1);
        if (c1 == '#') {
          i++;
          String comm;
          while (i < input.length() && input.charAt(i) != '\n') {
            comm += input.charAt(i++);
          }
          if (node) {
            TaggedTreeItemT<String> *c =
                new TaggedTreeItemT<String>(comm.trim());
            c->name = "_comment";
            node->add(c);
          }
          isNewLine = true;
          currentIndent = 0;
        } else if (c1 == '/' && c2 == '/') {
          i += 2;
          String comm;
          while (i < input.length() && input.charAt(i) != '\n') {
            comm += input.charAt(i++);
          }
          if (node) {
            TaggedTreeItemT<String> *c =
                new TaggedTreeItemT<String>(comm.trim());
            c->name = "_comment";
            node->add(c);
          }
          isNewLine = true;
          currentIndent = 0;
        } else if (c1 == '/' && c2 == '*') {
          i += 2;
          String comm;
          while (i + 1 < input.length() &&
                 !(input.charAt(i) == '*' && input.charAt(i + 1) == '/')) {
            comm += input.charAt(i++);
          }
          i += 2;
          if (node) {
            TaggedTreeItemT<String> *c =
                new TaggedTreeItemT<String>(comm.trim());
            c->name = "_comment";
            node->add(c);
          }
        } else {
          break;
        }
      } else if (i < input.length() && input.charAt(i) == '#') {
        i++; // last char comment
        break;
      } else {
        break;
      }
    }
  }

  String parseString() {
    String res;
    char quote = input.charAt(i);
    bool isQuoted = (quote == '\"' || quote == '\'');
    if (isQuoted) {
      i++;
      while (i < input.length()) {
        char c = input.charAt(i);
        if (c == quote) {
          i++;
          break;
        }
        if (c == '\\' && i + 1 < input.length()) {
          i++;
          char esc = input.charAt(i);
          if (esc == 'n')
            res += '\n';
          else if (esc == 'r')
            res += '\r';
          else if (esc == 't')
            res += '\t';
          else
            res += esc;
        } else {
          res += c;
        }
        i++;
      }
    } else {
      while (i < input.length()) {
        char c = input.charAt(i);
        if (c == ':' || c == ',' || c == ']' || c == '}' || c == '#' ||
            c == '\n' || c == '\r')
          break;
        res += c;
        i++;
      }
      res = res.trim();
    }
    return res;
  }

  TreeItem *parseValue(int parentIndent, TreeBranch *parentBranch = nullptr) {
    while (true) {
      skipComments(parentBranch);
      if (i >= input.length())
        return nullptr;

      // Skip Directives (%YAML, %TAG)
      if (input.charAt(i) == '%') {
        while (i < input.length() && input.charAt(i) != '\n')
          i++;
        isNewLine = true;
        currentIndent = 0;
        continue;
      }

      // Skip Document Separators (---)
      if (input.charAt(i) == '-' && i + 2 < input.length() &&
          input.charAt(i + 1) == '-' && input.charAt(i + 2) == '-') {
        i += 3;
        while (i < input.length() && input.charAt(i) != '\n')
          i++;
        isNewLine = true;
        currentIndent = 0;
        continue;
      }
      break;
    }

    if (i >= input.length())
      return nullptr;

    // Check for Anchors & Aliases
    if (input.charAt(i) == '&') {
      i++;
      String anchorName;
      while (i < input.length() && !isSpace(input.charAt(i)) &&
             input.charAt(i) != '\n') {
        anchorName += input.charAt(i++);
      }
      TreeItem *val = parseValue(parentIndent, parentBranch);
      if (val)
        anchors.set(anchorName, val);
      return val;
    }

    if (input.charAt(i) == '*') {
      i++;
      String aliasName;
      while (i < input.length() && !isSpace(input.charAt(i)) &&
             input.charAt(i) != '\n' && input.charAt(i) != ',' &&
             input.charAt(i) != ']' && input.charAt(i) != '}') {
        aliasName += input.charAt(i++);
      }
      TreeItem **ptr = (TreeItem **)anchors.get(aliasName);
      if (ptr && *ptr)
        return (*ptr)->clone();
      return new TaggedTreeItem(); // Fallback null
    }

    char c = input.charAt(i);

    // Sequence (Block style)
    if (c == '-' && (i + 1 >= input.length() || isSpace(input.charAt(i + 1)))) {
      TaggedTreeArrayBranch<TreeItem> *arr =
          new TaggedTreeArrayBranch<TreeItem>();
      int blockIndent = currentIndent;
      while (i < input.length()) {
        skipComments(arr);
        if (currentIndent < blockIndent)
          break;
        if (i < input.length() && input.charAt(i) == '-') {
          i++;
          TreeItem *item = parseValue(blockIndent + 1, arr);
          if (item)
            arr->add(item);
        } else
          break;
      }
      return arr;
    }

    // Sequence (Flow style / JSON)
    if (c == '[') {
      i++;
      TaggedTreeArrayBranch<TreeItem> *arr =
          new TaggedTreeArrayBranch<TreeItem>();
      while (i < input.length()) {
        skipComments(arr);
        if (i < input.length() && input.charAt(i) == ']') {
          i++;
          break;
        }
        if (i < input.length() && input.charAt(i) == ',') {
          i++;
          continue;
        }
        TreeItem *item = parseValue(-1, arr);
        if (item)
          arr->add(item);
      }
      return arr;
    }

    // Map (Flow style / JSON)
    if (c == '{') {
      i++;
      TaggedTreeBranch *obj = new TaggedTreeBranch();
      while (i < input.length()) {
        skipComments(obj);
        if (i < input.length() && input.charAt(i) == '}') {
          i++;
          break;
        }
        if (i < input.length() && input.charAt(i) == ',') {
          i++;
          continue;
        }
        String key = parseString();
        skipSpace();
        if (i < input.length() && input.charAt(i) == ':')
          i++;
        TreeItem *val = parseValue(-1, obj);
        if (val) {
          val->setName(key);
          obj->add(val);
        }
      }
      return obj;
    }

    // Block Mapping or Literal
    usz peek = i;
    bool isKey = false;
    int blockIndent = currentIndent;
    
    // Simple lookahead for a key
    while (peek < input.length() && input.charAt(peek) != '\n' &&
           input.charAt(peek) != '\r') {
      if (input.charAt(peek) == ':') {
        isKey = true;
        break;
      }
      peek++;
    }

    if (isKey) {
      TaggedTreeBranch *branch = new TaggedTreeBranch();
      while (i < input.length()) {
        skipComments(branch);
        if (currentIndent < blockIndent)
          break;
        if (i >= input.length())
          break;

        // Check for sequence start at same level (invalid for mapping)
        if (input.charAt(i) == '-' && (i + 1 >= input.length() || isSpace(input.charAt(i + 1))))
          break;

        // Parse Key
        String k;
        while (i < input.length() && input.charAt(i) != ':') {
           char ck = input.charAt(i++);
           if (ck == '\n') break; 
           k += ck;
        }
        if (i < input.length() && input.charAt(i) == ':') i++;
        k = k.trim();
        if (k.isEmpty()) break;

        // Parse Value
        TreeItem *v = parseValue(blockIndent, branch);
        if (v) {
          v->setName(k);
          branch->add(v);
        }

        // After value, we must be on a new line or at EOF
        skipComments(branch);
        if (currentIndent < blockIndent) break;
      }
      return branch;
    }

    // Scalar
    String s = parseString();
    if (s == "true")
      return new TaggedTreeItemT<bool>(true);
    if (s == "false")
      return new TaggedTreeItemT<bool>(false);
    if (s == "null" || s == "~")
      return new TaggedTreeItem();

    bool isNum = true;
    int dotCount = 0;
    for (usz k = 0; k < s.length(); ++k) {
      if (s.charAt(k) == '.')
        dotCount++;
      else if (s.charAt(k) < '0' || s.charAt(k) > '9') {
        if (k != 0 || s.charAt(k) != '-')
          isNum = false;
      }
    }
    if (isNum && s.length() > 0 && s != "-" && dotCount <= 1) {
      if (dotCount == 1)
        return new TaggedTreeItemT<f64>(s.toDouble());
      return new TaggedTreeItemT<long long>(s.toInt());
    }
    return new TaggedTreeItemT<String>(s);
  }
};

bool YAML::parse(const String &yaml, TreeItem &outRoot) {
  YamlParser p(yaml);
  TreeBranch *root = dynamic_cast<TreeBranch *>(&outRoot);
  if (!root)
    return false;

  while (p.i < yaml.length()) {
    TreeItem *res = p.parseValue(-1, root);
    if (!res)
      break;

    if (TreeBranch *b = dynamic_cast<TreeBranch *>(res)) {
      for (usz i = 0; i < b->size(); ++i) {
        root->add((*b)[i]->clone());
      }
      delete b;
    } else {
      root->add(res);
    }
    // ensure we advance or skip trailing space
    p.skipSpace();
  }
  return true;
}

static String emitValue(const TreeItem *node, int indentLevel, int indentSize) {
  if (!node)
    return "null";

  if (auto branch = dynamic_cast<const TaggedTreeBranch *>(node)) {
    String res;
    for (usz i = 0; i < branch->size(); ++i) {
      auto child = (*branch)[i];
      if (child->getName() == "_comment") {
        emitIdent(res, (indentLevel)*indentSize);
        res += "# " + dynamic_cast<const TreeItemT<String> *>(child)->value +
               "\n";
        continue;
      }
      emitIdent(res, indentLevel * indentSize);
      res += child->getName() + ": ";
      res += emitValue(child, indentLevel + 1, indentSize);
      res += "\n";
    }
    return res;
  }

  if (auto arr = dynamic_cast<const TreeBranch *>(node)) {
    // Simple check for array vs map if needed
    String res;
    bool first = true;
    for (usz i = 0; i < arr->size(); ++i) {
      TreeItem *child = (*arr)[i];
      if (child->getName() == "_comment") {
        emitIdent(res, (indentLevel)*indentSize);
        res += "# " + dynamic_cast<const TreeItemT<String> *>(child)->value +
               "\n";
        continue;
      }
      if (!first)
        res += "\n";
      emitIdent(res, indentLevel * indentSize);
      res += "- ";
      res += emitValue(child, indentLevel + 1, indentSize);
      first = false;
    }
    return res;
  }

  if (auto s = dynamic_cast<const TreeItemT<String> *>(node))
    return s->value;
  if (auto i = dynamic_cast<const TreeItemT<long long> *>(node))
    return String(i->value);
  if (auto b = dynamic_cast<const TreeItemT<bool> *>(node))
    return b->value ? "true" : "false";
  if (auto f = dynamic_cast<const TreeItemT<f64> *>(node))
    return String(f->value);

  return "null";
}

String YAML::toYAML(const TreeItem &root, int indent) {
  return emitValue(&root, 0, indent);
}

String YAML::toJSON(const TreeItem &root, int indent) {
  // For JSON, we can use a similar emitter that always uses flow style
  // For brevity, we'll use a placeholder until a full JSON emitter is needed
  return toYAML(root, indent); // Simplified
}

} // namespace Encoding
