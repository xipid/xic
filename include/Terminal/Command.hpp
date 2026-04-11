/**
 * @file Command.hpp
 * @brief The Xi command-line parser — fluid, chainable, zero-boilerplate.
 *
 * A Command is simultaneously the root parser, a sub-command scope,
 * an option result, and a separator result. Every method returns Command&
 * for chaining. Parsed data flows lazily — options are gathered on demand.
 *
 * Features:
 *   - Long/short flags, clusters, negation (--!flag, -!abc, --no-flag)
 *   - Comma-separated values (--flag=1,2,3)
 *   - Quoted multi-word tokens
 *   - Nested sub-commands
 *   - Stream splitting via separators (-- , --- etc.)
 *   - Default values & environment variable fallback
 *   - Auto help/version handling
 *   - Shell completion generation (bash/zsh/fish)
 *   - Typo suggestions for unknown options
 *
 * Usage:
 *   Command args(argc, argv);
 *   args.description("My app").version("1.0.0");
 *
 *   if (args.option("--help -h").description("Show help"))
 *       printf("%s", args.help().c_str());
 *
 *   String out = args.option("--output -o")
 *       .description("Output file")
 *       .defaults("stdout")
 *       .env("OUTPUT_PATH")
 *       .string();
 *
 *   Command& build = args.command("build b").description("Build project");
 *   if (build) { ... }
 */

#ifndef XI_TERMINAL_COMMAND_HPP
#define XI_TERMINAL_COMMAND_HPP

#include "../Collection/Map.hpp"
#include "../Collection/String.hpp"

using namespace Xi;
using namespace Collection;

namespace Terminal {

class XI_EXPORT Command {
public:
  // ─── Constructors ───────────────────────────────────────────────────

  Command();
  Command(const char *s);
  Command(const String &s);
  Command(int argc, char **argv);

  // ─── Parse ──────────────────────────────────────────────────────────

  Command &parse(const String &s);
  Command &parse(int argc, char **argv);
  Command &parse(const Array<String> &tokens);

  // ─── Metadata (chainable) ──────────────────────────────────────────

  Command &description(const String &s);
  Command &version(const String &s);
  Command &usage(const String &s);
  Command &named(const String &s);

  // ─── Options ────────────────────────────────────────────────────────

  /**
   * @brief Retrieve (and consume) an option from the parsed stream.
   * @param names  Space or comma separated aliases.
   *               e.g. "--alpha -a"  or  "--alpha,-a,--al"
   *               Short aliases like "-ab" expand to "-a" and "-b".
   * @return Command& with .value populated, .active set, chainable.
   *
   * Values are gathered from all matching flags:
   *   -a=1 -a=2 -a 3 --alpha 4  →  value = ["1","2","3","4"]
   *
   * Cluster behavior:
   *   -abcd=4  →  a=true b=true c=true d=4
   *
   * Negation:
   *   --!flag  -!f  --no-flag  →  value = ["false"]
   */
  Command &option(const String &names);

  // ─── Option modifiers (chainable after option()) ───────────────────

  /** @brief Set a default value if the option was not provided. */
  Command &defaults(const String &val);

  /** @brief Fall back to an environment variable if not provided on CLI. */
  Command &env(const String &varName);

  // ─── Commands ───────────────────────────────────────────────────────

  /**
   * @brief Register a sub-command.
   * @param names  Space or comma separated aliases.
   * @return Command& for the sub-command scope.
   *
   * Active only when matching the parent's primary() positional.
   * The sub-command receives all tokens after its matched alias.
   */
  Command &command(const String &names);

  // ─── Separators ─────────────────────────────────────────────────────

  /** @brief Set separator tokens that split the stream (e.g. "-- --- ;"). */
  Command &separators(const String &seps);

  /** @brief Get the sub-stream after a specific separator. */
  Command &separate(const String &seps);

  // ─── Querying ───────────────────────────────────────────────────────

  /** @brief First unclaimed positional (the "command" the user typed). */
  String primary() const;

  /** @brief All remaining unclaimed --x / -x flags (for error reporting). */
  Array<String> options() const;

  /** @brief All remaining unclaimed positional tokens. */
  Array<String> commands() const;

  /** @brief Access positional by index. */
  String operator[](usz i) const;

  // ─── Value accessors ───────────────────────────────────────────────

  /** @brief Join all values with comma. */
  String toString() const;

  /** @brief Get value at position as String. */
  String string(usz pos = 0) const;

  /** @brief Get value at position as integer. */
  long long integer(usz pos = 0) const;

  /** @brief Get value at position as double. */
  double number(usz pos = 0) const;

  /** @brief Get value at position as bool (true/1/on/yes → true). */
  bool boolean(usz pos = 0) const;

  /** @brief Number of values collected. */
  usz count() const;

  // ─── Truthiness ────────────────────────────────────────────────────

  /** @brief True if the option/command was activated by the user. */
  operator bool() const;

  // ─── Help & Completion ─────────────────────────────────────────────

  /** @brief Generate help text from all registered options/commands. */
  String help() const;

  /**
   * @brief Suggest the closest known option for a typo.
   * @param unknown  The unknown flag name (without dashes).
   * @return The closest registered option name, or "" if none close enough.
   */
  String suggest(const String &unknown) const;

  /** @brief Generate shell completion script (bash/zsh/fish). */
  void generateCompletion(const String &shell, const String &executable);

  // ─── Public state ──────────────────────────────────────────────────

  Array<String> value;                ///< Collected values for this scope.
  bool active = false;                ///< Whether this scope was matched.
  String name;                        ///< Display name (first alias).
  String desc;                        ///< Description string.

  Array<Command *> definedCommands;   ///< All registered sub-commands.
  Array<Command *> definedOptions;    ///< All registered options.

  Map<String, Command> separated;     ///< Separator-split sub-streams.

private:
  // ─── Internal types ────────────────────────────────────────────────

  struct OptionDef {
    Array<String> longNames;    // without --
    Array<String> shortNames;   // without -
    String displayName;         // for help text (e.g. "--output, -o")
    Command *result = nullptr;
  };

  struct CommandDef {
    Array<String> aliases;
    Command *result = nullptr;
  };

  // ─── Internal state ────────────────────────────────────────────────

  Array<String> _tokens;
  Array<String> _positionals;
  Map<String, Array<String>> _flags;  // canonical name → values

  Array<OptionDef> _optionDefs;
  Array<CommandDef> _commandDefs;
  Array<String> _separatorTokens;

  Map<String, Command> _subcommands;  // owned
  Map<String, Command> _optionObjs;   // owned

  String _version;
  String _usageStr;
  String _defaultVal;
  String _envVar;

  bool _parsed = false;
  bool _separated = false;

  mutable Map<String, bool> _consumed;

  // ─── Internals ─────────────────────────────────────────────────────

  static Array<String> _tokenize(const String &s);
  static Array<String> _splitNames(const String &names);
  static usz _editDistance(const String &a, const String &b);
  void _parse(const Array<String> &tokens);
  void _applySeparators();
  void _gatherOption(OptionDef &def);
  void _applyFallbacks();
  void _setFlag(const String &name, const String &val);
  void _consumeFlag(const String &name);
};

} // namespace Terminal

#endif // XI_TERMINAL_COMMAND_HPP
