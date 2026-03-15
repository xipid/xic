#ifndef REGEX_HPP
#define REGEX_HPP

#include "Array.hpp"
#include "Map.hpp"
#include "Primitives.hpp"
#include "String.hpp"

namespace Xi {

struct XI_EXPORT RegexMatch : public Array<String> {
  String full;
  long long start = -1;
  long long end = -1;
  Map<String, String> namedGroups;
  RegexMatch() = default;
};

} // namespace Xi

namespace Xi {

class XI_EXPORT Regex {
public:
  bool parsed = false;
  String code;
  bool globalIgnoreCase = false;
  bool dotAll = false;
  bool anchored = false;
  static constexpr int MAX_CAPS = 32;
  static constexpr int MAX_DFA_STATES = 1000;
  static constexpr int RECURSION_LIMIT = 512;

private:
  enum class Op {
    Match,
    Char,
    CharIC,
    Any,
    Class,
    Jmp,
    Split,
    Save,
    AssertStart,
    AssertEnd,
    AssertWordBound,
    Lookahead,
    NegLookahead,
    Lookbehind,
    NegLookbehind
  };

  struct Inst {
    Op op;
    int x = 0;
    int y = 0;
    bool invert = false;
    String chars;
    Array<Inst> sub;
  };

  Array<Inst> inst;
  struct CapName {
    String name;
    int idx;
  };
  Array<CapName> capNames;
  int numCaps = 1;

  String prefixLiteral;
  int skipTable[256];

  struct DFAState {
    Array<int> pcs;
    void sort() {
      for (int i = 0; i < (int)pcs.size(); i++) {
        for (int j = i + 1; j < (int)pcs.size(); j++) {
          if (pcs[i] > pcs[j]) {
            int t = pcs[i];
            pcs[i] = pcs[j];
            pcs[j] = t;
          }
        }
      }
    }
    bool operator==(const DFAState &o) const {
      if (pcs.size() != o.pcs.size())
        return false;
      for (usz i = 0; i < pcs.size(); i++)
        if (pcs[i] != o.pcs[i])
          return false;
      return true;
    }
  };

  mutable Array<DFAState> states;
  mutable Map<DFAState, int> stateMap;
  mutable int transitions[MAX_DFA_STATES][256];

  void resetDFA() const;

  void addEpsilon(int pc, Array<int> &set, Array<int> &visited) const;

  void addConsuming(int pc, Array<int> &set, Array<int> &visited) const;

  bool isWord(char c) const;

  bool checkClass(const Inst &ins, u8 c) const;

  int getNextDFAState(int stateId, u8 c) const;

  void emit(Array<Inst> &p, Op op, int x = 0, int y = 0);

  Array<Inst> compileSub(const String &p, int &pos);

  void addClassRange(Inst &ci, char start, char end);
  void addClassEscape(Inst &ci, char nc);

  void compileCore(const String &p, int &pos, Array<Inst> &prog, int depth = 0,
                   bool localIC = false);

  void buildSkipTable();

  void compile(const String &p);

  bool runVM(const String &in, int start, const Array<Inst> &prog,
             bool rev = false) const;

public:
  Regex(const String &p) { compile(p); }

  Array<RegexMatch> matchAll(const String &input, int maxMatches = 0,
                             u64 limitUs = 0) const;

  Array<String> split(const String &s) const;

  String replace(const String &s, const String &rep) const;
};

} // namespace Xi

#endif
