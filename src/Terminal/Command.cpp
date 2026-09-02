#include "../../include/Terminal/Command.hpp"
#include <cstdio>
#include <cstdlib>

namespace Terminal {

// ═══════════════════════════════════════════════════════════════════════
// Constructors
// ═══════════════════════════════════════════════════════════════════════

Command::Command() : active(false), _parsed(false), _separated(false) {}

Command::Command(const char *s) : Command() { parse(String(s)); }

Command::Command(const String &s) : Command() { parse(s); }

Command::Command(int argc, char **argv) : Command() { parse(argc, argv); }

Command::~Command() {
  for (usz i = 0; i < definedCommands.size(); ++i) {
    delete definedCommands[i];
  }
  definedCommands.clear();
  _subcommands.clear();
  for (usz i = 0; i < definedOptions.size(); ++i) {
    delete definedOptions[i];
  }
  definedOptions.clear();
  _optionObjs.clear();
  for (auto it = _separatedObjs.begin(); it != _separatedObjs.end(); ++it) {
    delete it->value;
  }
  _separatedObjs.clear();
}

// ═══════════════════════════════════════════════════════════════════════
// Parsing
// ═══════════════════════════════════════════════════════════════════════

Command &Command::parse(const String &s) { return parse(_tokenize(s)); }

Command &Command::parse(int argc, char **argv) {
  Array<String> tokens;
  for (int i = 1; i < argc; ++i)
    tokens.push(argv[i]);
  return parse(tokens);
}

Command &Command::parse(const Array<String> &tokens) {
  _tokens = tokens;
  _positionals.clear();
  _flags.clear();
  _consumed.clear();
  value.clear();
  _parsed = true;
  _separated = false;
  active = true;
  _parse(tokens);
  // Root value = positionals
  for (usz i = 0; i < _positionals.size(); ++i)
    value.push(_positionals[i]);
  return *this;
}

// ═══════════════════════════════════════════════════════════════════════
// Metadata
// ═══════════════════════════════════════════════════════════════════════

Command &Command::description(const String &s) { desc = s; return *this; }
Command &Command::version(const String &s) { _version = s; return *this; }
Command &Command::usage(const String &s) { _usageStr = s; return *this; }
Command &Command::named(const String &s) { name = s; return *this; }

// ═══════════════════════════════════════════════════════════════════════
// Option modifiers
// ═══════════════════════════════════════════════════════════════════════

Command &Command::defaults(const String &val) { _defaultVal = val; return *this; }

Command &Command::env(const String &varName) {
  _envVar = varName;
  _applyFallbacks();
  return *this;
}

void Command::_applyFallbacks() {
  if (active) return; // Already has user-provided values

  // Try environment variable
  if (!_envVar.isEmpty()) {
    const char *envVal = getenv(_envVar.c_str());
    if (envVal && envVal[0] != '\0') {
      value.clear();
      // Split by comma for consistency with CLI comma values
      String ev(envVal);
      if (ev.indexOf(',') != -1) {
        Array<String> parts = ev.split(",");
        for (usz i = 0; i < parts.size(); ++i)
          value.push(parts[i].trim());
      } else {
        value.push(ev);
      }
      active = true;
      return;
    }
  }
}

// ═══════════════════════════════════════════════════════════════════════
// Name splitting
// ═══════════════════════════════════════════════════════════════════════

Array<String> Command::_splitNames(const String &names) {
  Array<String> result;
  String current;
  for (usz i = 0; i < names.length(); ++i) {
    char c = names.charAt(i);
    if (c == ' ' || c == ',' || c == '\t') {
      String trimmed = current.trim();
      if (!trimmed.isEmpty()) result.push(trimmed);
      current = "";
    } else {
      current += c;
    }
  }
  String trimmed = current.trim();
  if (!trimmed.isEmpty()) result.push(trimmed);
  return result;
}

// ═══════════════════════════════════════════════════════════════════════
// Tokenizer — handles quotes, escapes
// ═══════════════════════════════════════════════════════════════════════

Array<String> Command::_tokenize(const String &s) {
  Array<String> tokens;
  String current;
  bool inQuotes = false;
  char quoteChar = 0;

  for (usz i = 0; i < s.length(); ++i) {
    char c = s.charAt(i);
    if (inQuotes) {
      if (c == '\\' && i + 1 < s.length()) {
        current += s.charAt(++i);
      } else if (c == quoteChar) {
        inQuotes = false;
        if (!current.isEmpty()) { tokens.push(current); current = ""; }
      } else {
        current += c;
      }
    } else {
      if (c == '"' || c == '\'') {
        if (!current.isEmpty()) { tokens.push(current); current = ""; }
        inQuotes = true;
        quoteChar = c;
      } else if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
        if (!current.isEmpty()) { tokens.push(current); current = ""; }
      } else {
        current += c;
      }
    }
  }
  if (!current.isEmpty()) tokens.push(current);
  return tokens;
}

// ═══════════════════════════════════════════════════════════════════════
// Core parser — single-pass, O(n)
// ═══════════════════════════════════════════════════════════════════════

void Command::_setFlag(const String &name, const String &val) {
  if (val.indexOf(',') != -1) {
    Array<String> vals = val.split(",");
    for (usz i = 0; i < vals.size(); ++i)
      _flags[name].push(vals[i].trim());
  } else {
    _flags[name].push(val);
  }
}

void Command::_consumeFlag(const String &name) { _consumed[name] = true; }

void Command::_parse(const Array<String> &tokens) {
  for (usz i = 0; i < tokens.size(); ++i) {
    const String &t = tokens[i];

    // ── --no-key negation (before general long option) ──
    if (t.startsWith("--no-") && t.length() > 5) {
      String key = t.substring(5);
      _setFlag(key, "false");
      continue;
    }

    // ── Long options: --key, --key=val, --!key ──
    if (t.startsWith("--") && t.length() > 2) {
      String kv = t.substring(2);
      bool negated = kv.startsWith("!");
      if (negated) kv = kv.substring(1);

      long long eqIdx = kv.indexOf('=');
      if (eqIdx != -1) {
        _setFlag(kv.substring(0, (usz)eqIdx),
                 negated ? "false" : kv.substring((usz)eqIdx + 1));
      } else if (!negated && i + 1 < tokens.size() &&
                 !tokens[i + 1].startsWith("-")) {
        _setFlag(kv, tokens[++i]);
      } else {
        _setFlag(kv, negated ? "false" : "true");
      }
      continue;
    }

    // ── Short options: -x, -xyz, -x=val, -!xyz ──
    if (t.startsWith("-") && t.length() > 1 && t.charAt(1) != '-') {
      String kv = t.substring(1);
      bool negated = kv.startsWith("!");
      if (negated) kv = kv.substring(1);
      if (kv.isEmpty()) continue;

      long long eqIdx = kv.indexOf('=');
      if (eqIdx != -1) {
        // Cluster + value: -abcd=4 → a=true b=true c=true d=4
        String cluster = kv.substring(0, (usz)eqIdx);
        String val = negated ? "false" : kv.substring((usz)eqIdx + 1);
        for (usz k = 0; k < cluster.length(); ++k) {
          String ch; ch += cluster.charAt(k);
          _setFlag(ch, (k + 1 == cluster.length()) ? val
                       : (negated ? "false" : "true"));
        }
      } else if (kv.length() == 1) {
        // Single short flag: -a or -a value
        if (!negated && i + 1 < tokens.size() &&
            !tokens[i + 1].startsWith("-")) {
          _setFlag(kv, tokens[++i]);
        } else {
          _setFlag(kv, negated ? "false" : "true");
        }
      } else {
        // Cluster: -abc → a=true b=true c=(nexttoken or true)
        for (usz k = 0; k < kv.length(); ++k) {
          String ch; ch += kv.charAt(k);
          if (negated) {
            _setFlag(ch, "false");
          } else {
            _setFlag(ch, "true");
          }
        }
      }
      continue;
    }

    // ── Positional ──
    _positionals.push(t);
  }
}

// ═══════════════════════════════════════════════════════════════════════
// Separator handling
// ═══════════════════════════════════════════════════════════════════════

Command &Command::separators(const String &seps) {
  _separatorTokens = _splitNames(seps);
  _separated = false;
  _applySeparators();
  return *this;
}

void Command::_applySeparators() {
  if (_separated || _separatorTokens.size() == 0) return;
  _separated = true;

  for (auto it = _separatedObjs.begin(); it != _separatedObjs.end(); ++it) {
    delete it->value;
  }
  _separatedObjs.clear();
  Array<String> currentTokens;
  String currentSep = "";

  for (usz i = 0; i < _tokens.size(); ++i) {
    bool isSep = false;
    for (usz j = 0; j < _separatorTokens.size(); ++j) {
      if (_tokens[i] == _separatorTokens[j]) {
        isSep = true;
        if (!_separatedObjs.has(currentSep)) {
          _separatedObjs[currentSep] = new Command();
        }
        _separatedObjs[currentSep]->parse(currentTokens);
        currentTokens.clear();
        currentSep = _tokens[i];
        break;
      }
    }
    if (!isSep) currentTokens.push(_tokens[i]);
  }
  if (!_separatedObjs.has(currentSep)) {
    _separatedObjs[currentSep] = new Command();
  }
  _separatedObjs[currentSep]->parse(currentTokens);

  // Update root value to pre-separator tokens only
  if (_separatedObjs.has("")) {
    value.clear();
    Command *root = _separatedObjs[""];
    for (usz i = 0; i < root->_positionals.size(); ++i)
      value.push(root->_positionals[i]);
  }
}

Command &Command::separate(const String &seps) {
  Array<String> sepList = _splitNames(seps);
  for (usz i = 0; i < sepList.size(); ++i) {
    bool found = false;
    for (usz j = 0; j < _separatorTokens.size(); ++j) {
      if (_separatorTokens[j] == sepList[i]) { found = true; break; }
    }
    if (!found) _separatorTokens.push(sepList[i]);
  }
  _separated = false;
  _applySeparators();

  String firstSep = sepList.size() > 0 ? sepList[0] : "";
  if (!_separatedObjs.has(firstSep)) {
    _separatedObjs[firstSep] = new Command();
  }
  return *_separatedObjs[firstSep];
}

// ═══════════════════════════════════════════════════════════════════════
// Option gathering — lazy, consuming
// ═══════════════════════════════════════════════════════════════════════

void Command::_gatherOption(OptionDef &def) {
  Command *opt = def.result;
  opt->value.clear();
  opt->active = false;

  // Gather from long names
  for (usz i = 0; i < def.longNames.size(); ++i) {
    const String &n = def.longNames[i];
    if (_consumed.has(n)) continue;
    Array<String> *vals = _flags.get(n);
    if (vals) {
      opt->active = true;
      for (usz j = 0; j < vals->size(); ++j) opt->value.push((*vals)[j]);
      _consumeFlag(n);
    }
  }

  // Gather from short names
  for (usz i = 0; i < def.shortNames.size(); ++i) {
    const String &n = def.shortNames[i];
    if (_consumed.has(n)) continue;
    Array<String> *vals = _flags.get(n);
    if (vals) {
      opt->active = true;
      for (usz j = 0; j < vals->size(); ++j) opt->value.push((*vals)[j]);
      _consumeFlag(n);
    }
  }

  // Apply default if no values and a default is set
  if (!opt->active && !opt->_defaultVal.isEmpty()) {
    opt->value.push(opt->_defaultVal);
    // active stays false — user didn't provide it
  }
}

Command &Command::option(const String &names) {
  // Idempotent: return cached result if same names string
  if (_optionObjs.has(names))
    return *_optionObjs[names];

  OptionDef def;
  Array<String> parts = _splitNames(names);
  String displayParts;

  for (usz i = 0; i < parts.size(); ++i) {
    const String &p = parts[i];
    if (p.startsWith("--")) {
      def.longNames.push(p.substring(2));
      if (!displayParts.isEmpty()) displayParts += ", ";
      displayParts += p;
    } else if (p.startsWith("-")) {
      String stripped = p.substring(1);
      for (usz k = 0; k < stripped.length(); ++k) {
        String ch; ch += stripped.charAt(k);
        def.shortNames.push(ch);
      }
      if (!displayParts.isEmpty()) displayParts += ", ";
      displayParts += p;
    } else {
      // Bare name → long
      def.longNames.push(p);
      if (!displayParts.isEmpty()) displayParts += ", ";
      displayParts += "--" + p;
    }
  }

  def.displayName = displayParts;

  Command *opt = new Command();
  opt->name = displayParts;
  opt->active = false;
  _optionObjs[names] = opt;
  def.result = opt;

  _gatherOption(def);

  _optionDefs.push(def);
  definedOptions.push(opt);

  return *opt;
}

Command &Command::flag(const String &names) {
  Command &opt = option(names);
  if (opt.active && opt.value.size() > 0) {
    String val = opt.value[0];
    if (val != "true" && val != "false" && val != "1" && val != "0" && val != "on" && val != "off" && val != "yes" && val != "no") {
      // Swallowed a positional! Un-swallow it.
      _positionals.unshift(val);
      opt.value.clear();
      opt.value.push("true");
    }
  }
  return opt;
}

// ═══════════════════════════════════════════════════════════════════════
// Commands
// ═══════════════════════════════════════════════════════════════════════

Command &Command::command(const String &names) {
  if (_subcommands.has(names))
    return *_subcommands[names];

  CommandDef def;
  Array<String> parts = _splitNames(names);
  String firstName = parts.size() > 0 ? parts[0] : names;

  for (usz i = 0; i < parts.size(); ++i)
    def.aliases.push(parts[i]);

  Command *cmd = new Command();
  cmd->name = firstName;
  cmd->active = false;
  _subcommands[names] = cmd;
  def.result = cmd;

  // Check if any alias matches the primary positional
  String prim = primary();
  for (usz i = 0; i < def.aliases.size(); ++i) {
    if (def.aliases[i] == prim) {
      cmd->active = true;
      // Collect all tokens AFTER the matched command token
      Array<String> subTokens;
      bool found = false;
      for (usz j = 0; j < _tokens.size(); ++j) {
        if (found) {
          subTokens.push(_tokens[j]);
        } else if (_tokens[j] == prim) {
          found = true;
        }
      }
      cmd->parse(subTokens);
      break;
    }
  }

  _commandDefs.push(def);
  definedCommands.push(cmd);

  return *cmd;
}

// ═══════════════════════════════════════════════════════════════════════
// Querying
// ═══════════════════════════════════════════════════════════════════════

String Command::primary() const {
  return _positionals.size() > 0 ? _positionals[0] : "";
}

Array<String> Command::options() const {
  Array<String> result;
  for (auto &kv : _flags) {
    if (!_consumed.has(kv.key)) {
      // Reconstruct with proper prefix
      result.push((kv.key.length() > 1 ? "--" : "-") + kv.key);
    }
  }
  return result;
}

Array<String> Command::commands() const {
  Array<String> result;
  for (usz i = 0; i < _positionals.size(); ++i)
    result.push(_positionals[i]);
  return result;
}

String Command::operator[](usz i) const {
  return (i < _positionals.size()) ? _positionals[i] : "";
}

// ═══════════════════════════════════════════════════════════════════════
// Value accessors
// ═══════════════════════════════════════════════════════════════════════

String Command::toString() const {
  String res;
  for (usz i = 0; i < value.size(); ++i) {
    if (i > 0) res += ",";
    res += value[i];
  }
  return res;
}

String Command::string(usz pos) const {
  return (pos < value.size()) ? value[pos] : _defaultVal;
}

long long Command::integer(usz pos) const {
  String s = string(pos);
  return s.isEmpty() ? 0 : s.toInt();
}

double Command::number(usz pos) const {
  String s = string(pos);
  return s.isEmpty() ? 0.0 : s.toDouble();
}

bool Command::boolean(usz pos) const {
  String s = string(pos);
  if (s.isEmpty()) return false;
  return (s == "true" || s == "1" || s == "on" || s == "yes");
}

usz Command::count() const { return value.size(); }

Command::operator bool() const { return active; }

// ═══════════════════════════════════════════════════════════════════════
// Edit distance for typo suggestions
// ═══════════════════════════════════════════════════════════════════════

usz Command::_editDistance(const String &a, const String &b) {
  usz la = a.length(), lb = b.length();
  if (la == 0) return lb;
  if (lb == 0) return la;

  // Use two rows to save memory
  usz *prev = new usz[lb + 1];
  usz *curr = new usz[lb + 1];

  for (usz j = 0; j <= lb; ++j) prev[j] = j;

  for (usz i = 1; i <= la; ++i) {
    curr[0] = i;
    for (usz j = 1; j <= lb; ++j) {
      usz cost = (a.charAt(i - 1) == b.charAt(j - 1)) ? 0 : 1;
      usz del = prev[j] + 1;
      usz ins = curr[j - 1] + 1;
      usz sub = prev[j - 1] + cost;
      curr[j] = del < ins ? (del < sub ? del : sub) : (ins < sub ? ins : sub);
    }
    usz *tmp = prev; prev = curr; curr = tmp;
  }

  usz result = prev[lb];
  delete[] prev;
  delete[] curr;
  return result;
}

String Command::suggest(const String &unknown) const {
  String best;
  usz bestDist = (usz)-1;

  // Strip dashes from unknown
  String stripped = unknown;
  if (stripped.startsWith("--")) stripped = stripped.substring(2);
  else if (stripped.startsWith("-")) stripped = stripped.substring(1);

  for (usz i = 0; i < _optionDefs.size(); ++i) {
    const OptionDef &def = _optionDefs[i];
    for (usz j = 0; j < def.longNames.size(); ++j) {
      usz d = _editDistance(stripped, def.longNames[j]);
      if (d < bestDist && d <= 3) { bestDist = d; best = "--" + def.longNames[j]; }
    }
    for (usz j = 0; j < def.shortNames.size(); ++j) {
      usz d = _editDistance(stripped, def.shortNames[j]);
      if (d < bestDist && d <= 2) { bestDist = d; best = "-" + def.shortNames[j]; }
    }
  }
  return best;
}

// ═══════════════════════════════════════════════════════════════════════
// Help generation
// ═══════════════════════════════════════════════════════════════════════

String Command::help() const {
  String out;

  // Usage line
  if (!_usageStr.isEmpty()) {
    out += _usageStr;
  } else {
    out += "Usage:";
    if (!name.isEmpty()) out += " " + name;
    if (definedOptions.size() > 0) out += " [options]";
    if (definedCommands.size() > 0) out += " [command]";
  }
  out += "\n";

  if (!desc.isEmpty())
    out += "\n" + desc + "\n";

  if (definedCommands.size() > 0) {
    out += "\nCommands:\n";
    for (usz i = 0; i < definedCommands.size(); ++i) {
      Command *cmd = definedCommands[i];
      out += "  " + cmd->name.padEnd(24) + " " + cmd->desc + "\n";
    }
  }

  if (definedOptions.size() > 0) {
    out += "\nOptions:\n";
    for (usz i = 0; i < definedOptions.size(); ++i) {
      Command *opt = definedOptions[i];
      out += "  " + opt->name.padEnd(24) + " " + opt->desc + "\n";
    }
  }

  if (!_version.isEmpty())
    out += "\nVersion: " + _version + "\n";

  return out;
}



// ═══════════════════════════════════════════════════════════════════════
// Shell completion generation
// ═══════════════════════════════════════════════════════════════════════

void Command::generateCompletion(const String &shell,
                                 const String &executable) {
  if (shell == "zsh") {
    printf("# zsh completion for %s\n", executable.c_str());
    printf("compdef _%s %s\n\n", executable.c_str(), executable.c_str());
    printf("_%s() {\n", executable.c_str());
    printf("  _arguments -s \\\n");
    for (usz i = 0; i < definedOptions.size(); ++i) {
      Command *opt = definedOptions[i];
      printf("    '%s[%s]' \\\n", opt->name.c_str(), opt->desc.c_str());
    }
    if (definedCommands.size() > 0) {
      printf("    '1:command:(");
      for (usz i = 0; i < definedCommands.size(); ++i) {
        if (i > 0) printf(" ");
        printf("%s", definedCommands[i]->name.c_str());
      }
      printf(")' \\\n");
    }
    printf("    '*: :_files'\n}\n");

  } else if (shell == "bash") {
    printf("# bash completion for %s\n", executable.c_str());
    printf("_%s_completions() {\n", executable.c_str());
    String opts;
    for (usz i = 0; i < definedOptions.size(); ++i)
      opts += definedOptions[i]->name + " ";
    for (usz i = 0; i < definedCommands.size(); ++i)
      opts += definedCommands[i]->name + " ";
    printf("  COMPREPLY=($(compgen -W \"%s\" -- "
           "\"${COMP_WORDS[COMP_CWORD]}\"))\n", opts.trim().c_str());
    printf("}\ncomplete -F _%s_completions %s\n",
           executable.c_str(), executable.c_str());

  } else if (shell == "fish") {
    printf("# fish completion for %s\n", executable.c_str());
    for (usz i = 0; i < definedOptions.size(); ++i) {
      Command *opt = definedOptions[i];
      // Extract first long name for -l flag
      for (usz j = 0; j < _optionDefs.size(); ++j) {
        if (_optionDefs[j].result == opt && _optionDefs[j].longNames.size()) {
          printf("complete -c %s -l %s -d '%s'\n",
                 executable.c_str(),
                 _optionDefs[j].longNames[0].c_str(),
                 opt->desc.c_str());
          break;
        }
      }
    }
    for (usz i = 0; i < definedCommands.size(); ++i)
      printf("complete -c %s -a %s -d '%s'\n", executable.c_str(),
             definedCommands[i]->name.c_str(), definedCommands[i]->desc.c_str());
  }
}

} // namespace Terminal
