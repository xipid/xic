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
  Map<String, NodeBase *> anchors;

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

  void skipComments(NodeBase *node = nullptr) {
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
            NamedNode<String> *c =
                new NamedNode<String>(comm.trim());
            c->name = "_comment";
            node->add(c);
          }
          isNewLine = true;
          currentIndent = 0;
        } else if (c1 == '/' && c2 == '/' && (i == 0 || input.charAt(i - 1) == ' ' || input.charAt(i - 1) == '\t' || input.charAt(i - 1) == '\n')) {
          i += 2;
          String comm;
          while (i < input.length() && input.charAt(i) != '\n') {
            comm += input.charAt(i++);
          }
          if (node) {
            NamedNode<String> *c =
                new NamedNode<String>(comm.trim());
            c->name = "_comment";
            node->add(c);
          }
          isNewLine = true;
          currentIndent = 0;
        } else if (c1 == '/' && c2 == '*' && (i == 0 || input.charAt(i - 1) == ' ' || input.charAt(i - 1) == '\t' || input.charAt(i - 1) == '\n')) {
          i += 2;
          String comm;
          while (i + 1 < input.length() &&
                 !(input.charAt(i) == '*' && input.charAt(i + 1) == '/')) {
            comm += input.charAt(i++);
          }
          i += 2;
          if (node) {
            NamedNode<String> *c =
                new NamedNode<String>(comm.trim());
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

  NodeBase *parseValue(int parentIndent, NodeBase *parentBranch = nullptr) {
    while (true) {
      skipComments(parentBranch);
      if (i >= input.length())
        return nullptr;

      // Skip Directives (%YAML, %TAG) at root level
      if (currentIndent == 0 && input.charAt(i) == '%' &&
          (input.indexOf("%YAML", i) == i || input.indexOf("%TAG", i) == i)) {
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
      NodeBase *val = parseValue(parentIndent, parentBranch);
      if (val)
        anchors.put(anchorName, val);
      return val;
    }
    if (input.charAt(i) == '*') {
      i++;
      String aliasName;
      while (i < input.length() && !isSpace(input.charAt(i)) &&
             input.charAt(i) != '\n') {
        aliasName += input.charAt(i++);
      }
      NodeBase **ref = anchors.get(aliasName);
      if (ref && *ref)
        return (*ref)->clone();
      return nullptr;
    }

    int blockIndent = currentIndent;

    // Flow Style Sequence
    if (input.charAt(i) == '[') {
      i++;
      NamedNode<void> *arr = new NamedNode<void>();
      while (i < input.length()) {
        skipComments(arr);
        if (input.charAt(i) == ']') {
          i++;
          break;
        }
        NodeBase *val = parseValue(blockIndent, arr);
        if (val)
          arr->add(val);
        skipSpace();
        if (input.charAt(i) == ',') {
          i++;
        }
      }
      return arr;
    }

    // Flow Style Mapping
    if (input.charAt(i) == '{') {
      i++;
      NamedNode<void> *mapNode = new NamedNode<void>();
      while (i < input.length()) {
        skipComments(mapNode);
        if (input.charAt(i) == '}') {
          i++;
          break;
        }
        String k = parseString();
        skipSpace();
        if (input.charAt(i) == ':') {
          i++;
        }
        NodeBase *val = parseValue(blockIndent, mapNode);
        if (val) {
          val->setName(k);
          mapNode->add(val);
        }
        skipSpace();
        if (input.charAt(i) == ',') {
          i++;
        }
      }
      return mapNode;
    }

    // Block Sequence
    if (input.charAt(i) == '-' && (i + 1 < input.length() && isSpace(input.charAt(i + 1)))) {
      NamedNode<void> *arr = new NamedNode<void>();
      int seqIndent = currentIndent;
      while (i < input.length()) {
        skipComments(arr);
        if (currentIndent < seqIndent)
          break;
        if (input.charAt(i) != '-')
          break;
        i++; // skip '-'
        skipSpace();
        NodeBase *val = parseValue(seqIndent + 1, arr);
        if (val)
          arr->add(val);
        skipComments(arr);
      }
      return arr;
    }


    // Look ahead to check if this is a key-value mapping
    bool isKey = false;
    usz peek = i;
    if (peek < input.length() && (input.charAt(peek) == '\"' || input.charAt(peek) == '\'')) {
      char q = input.charAt(peek++);
      while (peek < input.length()) {
        if (input.charAt(peek) == '\\' && peek + 1 < input.length()) {
          peek += 2;
          continue;
        }
        if (input.charAt(peek) == q) {
          peek++;
          break;
        }
        peek++;
      }
      while (peek < input.length() && isSpace(input.charAt(peek))) peek++;
      if (peek < input.length() && input.charAt(peek) == ':' &&
          (peek + 1 >= input.length() || isSpace(input.charAt(peek + 1)) || input.charAt(peek + 1) == '\n' || input.charAt(peek + 1) == '\r')) {
        isKey = true;
      }
    } else {
      while (peek < input.length()) {
        char cp = input.charAt(peek);
        if (cp == '\n' || cp == '\r')
          break;
        if (cp == ':' && (peek + 1 >= input.length() || isSpace(input.charAt(peek + 1)) || input.charAt(peek + 1) == '\n' || input.charAt(peek + 1) == '\r')) {
          isKey = true;
          break;
        }
        peek++;
      }
    }

    if (isKey) {
      NamedNode<void> *branch = new NamedNode<void>();
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
        if (input.charAt(i) == '\"' || input.charAt(i) == '\'') {
          k = parseString();
          skipSpace();
          if (i < input.length() && input.charAt(i) == ':') i++;
        } else {
          while (i < input.length()) {
            char ck = input.charAt(i);
            if (ck == '\n' || ck == '\r') break;
            if (ck == ':' && (i + 1 >= input.length() || isSpace(input.charAt(i + 1)) || input.charAt(i + 1) == '\n' || input.charAt(i + 1) == '\r')) {
              i++;
              break;
            }
            k += ck;
            i++;
          }
        }
        k = k.trim();
        if (k.isEmpty()) break;

        // Parse Value
        NodeBase *v = parseValue(blockIndent, branch);
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
      return new NamedNode<bool>(true);
    if (s == "false")
      return new NamedNode<bool>(false);
    if (s == "null" || s == "~")
      return new NamedNode<void>();

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
        return new NamedNode<f64>(s.toDouble());
      return new NamedNode<long long>(s.toInt());
    }
    return new NamedNode<String>(s);
  }
};

bool YAML::parse(const String &yaml, NodeBase &outRoot) {
  YamlParser p(yaml);
  NodeBase *root = &outRoot;
  if (!root)
    return false;

  while (p.i < yaml.length()) {
    NodeBase *res = p.parseValue(-1, root);
    if (!res)
      break;

    if (res) {
      if (res->isContainer()) {
        for (usz i = 0; i < res->size(); ++i) {
          root->add((*res)[i]->clone());
        }
        delete res;
      } else {
        root->add(res);
      }
    }
    // ensure we advance or skip trailing space
    p.skipSpace();
  }
  return true;
}

static String emitValue(const NodeBase *node, int indentLevel, int indentSize, bool firstLineNoIndent = false) {
  if (!node)
    return "null";

  if (!node->isContainer()) {
    if (auto s = dynamic_cast<const Node<String> *>(node))
      return s->value;
    if (auto i = dynamic_cast<const Node<long long> *>(node))
      return String(i->value);
    if (auto b = dynamic_cast<const Node<bool> *>(node))
      return b->value ? "true" : "false";
    if (auto f = dynamic_cast<const Node<f64> *>(node))
      return String(f->value);
    return "null";
  }

  if (auto branch = dynamic_cast<const NamedNode<void> *>(node)) {
    String res;
    for (usz i = 0; i < branch->size(); ++i) {
      auto child = (*branch)[i];
      if (child->getName() == "_comment") {
        if (!(i == 0 && firstLineNoIndent)) {
          emitIdent(res, (indentLevel)*indentSize);
        }
        res += "# " + dynamic_cast<const Node<String> *>(child)->value +
               "\n";
        continue;
      }
      if (!(i == 0 && firstLineNoIndent)) {
        emitIdent(res, (indentLevel)*indentSize);
      }
      res += child->getName() + ":";
      if (child->size() > 0) {
        res += "\n";
        res += emitValue(child, indentLevel + 1, indentSize);
      } else {
        res += " " + emitValue(child, indentLevel, indentSize, true) + "\n";
      }
    }
    return res;
  }

  if (auto arr = node) {
    String res;
    bool first = true;
    for (usz i = 0; i < arr->size(); ++i) {
      NodeBase *child = (*arr)[i];
      if (child->getName() == "_comment") {
        if (!(first && firstLineNoIndent)) {
          emitIdent(res, (indentLevel)*indentSize);
        }
        res += "# " + dynamic_cast<const Node<String> *>(child)->value +
               "\n";
        continue;
      }
      if (!(first && firstLineNoIndent)) {
        emitIdent(res, (indentLevel)*indentSize);
      }
      first = false;
      res += "- " + emitValue(child, indentLevel + 1, indentSize, true) + "\n";
    }
    return res;
  }

  return "null";
}

String YAML::toYAML(const NodeBase &root, int indent) {
  return emitValue(&root, 0, indent);
}

String YAML::toJSON(const NodeBase &root, int indent) {
  // For JSON, we can use a similar emitter that always uses flow style
  // For brevity, we'll use a placeholder until a full JSON emitter is needed
  return toYAML(root, indent); // Simplified
}

} // namespace Encoding
