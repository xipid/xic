#ifndef REGEX_HPP
#define REGEX_HPP

#include "Array.hpp"
#include "Map.hpp"
#include "Primitives.hpp"
#include "String.hpp"

namespace Xi {

struct RegexMatch : public Array<String> {
  String full;
  long long start = -1;
  long long end = -1;
  Map<String, String> namedGroups;
  RegexMatch() = default;
};

} // namespace Xi

namespace Xi {

class Regex {
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

  void resetDFA() const {
    states = Array<DFAState>();
    stateMap = Map<DFAState, int>();
    for (int i = 0; i < MAX_DFA_STATES; i++)
      for (int j = 0; j < 256; j++)
        transitions[i][j] = -1;
    DFAState initial;
    Array<int> vis;
    addEpsilon(0, initial.pcs, vis);
    initial.sort();
    states.push(Xi::Move(initial));
    stateMap.set(states[0], 0);
  }

  void addEpsilon(int pc, Array<int> &set, Array<int> &visited) const {
    if (pc < 0 || pc >= (int)inst.size() || visited.find(pc) != -1)
      return;
    visited.push(pc);
    Op op = inst[(usz)pc].op;
    if (op == Op::Split) {
      addEpsilon(inst[(usz)pc].x, set, visited);
      addEpsilon(inst[(usz)pc].y, set, visited);
    } else if (op == Op::Jmp) {
      addEpsilon(inst[(usz)pc].x, set, visited);
    } else {
      set.push(pc);
    }
  }

  void addConsuming(int pc, Array<int> &set, Array<int> &visited) const {
    if (pc < 0 || pc >= (int)inst.size() || visited.find(pc) != -1)
      return;
    visited.push(pc);
    Op op = inst[(usz)pc].op;
    if (op == Op::Split) {
      addConsuming(inst[(usz)pc].x, set, visited);
      addConsuming(inst[(usz)pc].y, set, visited);
    } else if (op == Op::Jmp) {
      addConsuming(inst[(usz)pc].x, set, visited);
    } else if (op == Op::Save || op == Op::AssertStart || op == Op::AssertEnd ||
               op == Op::AssertWordBound || op == Op::Lookahead ||
               op == Op::NegLookahead || op == Op::Lookbehind ||
               op == Op::NegLookbehind) {
      addConsuming(pc + 1, set, visited);
    } else {
      set.push(pc);
    }
  }

  bool isWord(char c) const {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
           (c >= '0' && c <= '9') || c == '_';
  }

  bool checkClass(const Inst &ins, u8 c) const {
    if (ins.chars.isEmpty())
      return false;
    bool ok = false;
    if (ins.chars == "w")
      ok = isWord((char)c);
    else if (ins.chars == "d")
      ok = (c >= '0' && c <= '9');
    else if (ins.chars == "s")
      ok = (c == ' ' || c == '\t' || c == '\n' || c == '\r');
    else
      ok = (ins.chars.indexOf((char)c) != -1);
    return ins.invert ? !ok : ok;
  }

  int getNextDFAState(int stateId, u8 c) const {
    if (transitions[stateId][c] != -1)
      return transitions[stateId][c];
    const DFAState &current = states[(usz)stateId];
    DFAState next;
    Array<int> nextSet;
    for (int pc : current.pcs) {
      Op op = inst[(usz)pc].op;
      if (op == Op::Char && (u8)inst[(usz)pc].x == c) {
        Array<int> v;
        addConsuming(pc + 1, nextSet, v);
      } else if (op == Op::CharIC) {
        int tg = inst[(usz)pc].x;
        if (tg >= 'a' && tg <= 'z')
          tg -= 32;
        u8 uc = (c >= 'a' && c <= 'z') ? c - 32 : c;
        if (uc == (u8)tg) {
          Array<int> v;
          addConsuming(pc + 1, nextSet, v);
        }
      } else if (op == Op::Any && (c != 0 || dotAll)) {
        Array<int> v;
        addConsuming(pc + 1, nextSet, v);
      } else if (op == Op::Class) {
        if (checkClass(inst[(usz)pc], c)) {
          Array<int> v;
          addConsuming(pc + 1, nextSet, v);
        }
      }
    }
    if (nextSet.size() == 0) {
      transitions[stateId][c] = -2;
      return -2;
    }
    next.pcs = Xi::Move(nextSet);
    next.sort();
    int *idFixed = stateMap.get(next);
    if (idFixed) {
      transitions[stateId][c] = *idFixed;
      return *idFixed;
    }
    if (states.size() >= MAX_DFA_STATES) {
      resetDFA();
      return -1;
    }
    int newId = (int)states.size();
    states.push(Xi::Move(next));
    stateMap.set(states[(usz)newId], newId);
    transitions[stateId][c] = newId;
    return newId;
  }

  void emit(Array<Inst> &p, Op op, int x = 0, int y = 0) {
    Inst i;
    i.op = op;
    i.x = x;
    i.y = y;
    p.push(Xi::Move(i));
  }

  Array<Inst> compileSub(const String &p, int &pos) {
    Array<Inst> s;
    compileCore(p, pos, s, 0);
    emit(s, Op::Match);
    return s;
  }

  void addClassRange(Inst &ci, char start, char end) {
    for (char rc = start; rc <= end; rc++)
      ci.chars += rc;
  }
  void addClassEscape(Inst &ci, char nc) {
    if (nc == 'd')
      addClassRange(ci, '0', '9');
    else if (nc == 'w') {
      addClassRange(ci, 'a', 'z');
      addClassRange(ci, 'A', 'Z');
      addClassRange(ci, '0', '9');
      ci.chars += '_';
    } else if (nc == 's') {
      ci.chars += ' ';
      ci.chars += '\t';
      ci.chars += '\n';
      ci.chars += '\r';
    } else if (nc == 't')
      ci.chars += '\t';
    else if (nc == 'n')
      ci.chars += '\n';
    else if (nc == 'r')
      ci.chars += '\r';
    else
      ci.chars += nc;
  }

  void compileCore(const String &p, int &pos, Array<Inst> &prog, int depth = 0,
                   bool localIC = false) {
    if (depth > RECURSION_LIMIT)
      return;
    int coreIdx = (int)prog.size();
    while (pos < (int)p.size() && p[pos] != ')' && p[pos] != '|') {
      int itemStart = (int)prog.size();
      char c = p[pos++];
      if (c == '^')
        emit(prog, Op::AssertStart);
      else if (c == '$')
        emit(prog, Op::AssertEnd);
      else if (c == '.')
        emit(prog, Op::Any);
      else if (c == '\\' && pos < (int)p.size()) {
        char nc = p[pos++];
        if (nc == 'w' || nc == 'd' || nc == 's' || nc == 'b') {
          Inst ci;
          ci.op = (nc == 'b') ? Op::AssertWordBound : Op::Class;
          ci.chars += nc;
          prog.push(Xi::Move(ci));
        } else {
          emit(prog, localIC || globalIgnoreCase ? Op::CharIC : Op::Char,
               (int)nc);
        }
      } else if (c == '[') {
        Inst ci;
        ci.op = Op::Class;
        ci.invert = false;
        if (pos < (int)p.size() && p[pos] == '^') {
          ci.invert = true;
          pos++;
        }
        while (pos < (int)p.size() && p[pos] != ']') {
          char val = p[pos++];
          if (val == '\\' && pos < (int)p.size()) {
            addClassEscape(ci, p[pos++]);
          } else if (pos + 1 < (int)p.size() && p[pos] == '-' &&
                     p[pos + 1] != ']') {
            pos++;
            char end = p[pos++];
            addClassRange(ci, val, end);
          } else {
            ci.chars += val;
          }
        }
        if (pos < (int)p.size())
          pos++;
        prog.push(Xi::Move(ci));
      } else if (c == '(') {
        bool cap = true;
        String name = "";
        bool nextIC = localIC;
        if (pos < (int)p.size() && p[pos] == '?') {
          pos++;
          if (p[pos] == 'P' || p[pos] == '<') {
            if (p[pos] == 'P')
              pos++;
            if (p[pos] == '<') {
              pos++;
              while (pos < (int)p.size() && p[pos] != '>')
                name += p[pos++];
              if (pos < (int)p.size())
                pos++;
            }
          } else if (p[pos] == ':') {
            cap = false;
            pos++;
          } else if (p[pos] == 'i') {
            pos++;
            nextIC = true;
            if (p[pos] == ':')
              pos++;
            cap = false;
          } else if (p[pos] == 's') {
            pos++;
            dotAll = true;
            if (p[pos] == ':')
              pos++;
            cap = false;
          } else if (p[pos] == '=') {
            pos++;
            emit(prog, Op::Lookahead);
            cap = false;
            prog[(usz)prog.size() - 1].sub = compileSub(p, pos);
          } else if (p[pos] == '!') {
            pos++;
            emit(prog, Op::NegLookahead);
            cap = false;
            prog[(usz)prog.size() - 1].sub = compileSub(p, pos);
          } else if (p[pos] == '<' && pos + 1 < (int)p.size()) {
            pos++;
            char t = p[pos++];
            if (t == '=') {
              emit(prog, Op::Lookbehind);
              prog[(usz)prog.size() - 1].sub = compileSub(p, pos);
            } else if (t == '!') {
              emit(prog, Op::NegLookbehind);
              prog[(usz)prog.size() - 1].sub = compileSub(p, pos);
            }
            cap = false;
          }
        }
        int idx = 0;
        if (cap) {
          idx = numCaps++;
          if (!name.isEmpty()) {
            CapName cn;
            cn.name = name;
            cn.idx = idx;
            capNames.push(cn);
          }
          emit(prog, Op::Save, idx * 2);
        }
        compileCore(p, pos, prog, depth + 1, nextIC);
        if (cap)
          emit(prog, Op::Save, idx * 2 + 1);
        if (pos < (int)p.size() && p[pos] == ')')
          pos++;
      } else {
        emit(prog, localIC || globalIgnoreCase ? Op::CharIC : Op::Char, (int)c);
      }

      if (pos < (int)p.size()) {
        char q = p[pos];
        if (q == '*' || q == '+' || q == '?' || q == '{') {
          pos++;
          bool greedy = true;
          if (pos < (int)p.size() && p[pos] == '?') {
            greedy = false;
            pos++;
          }
          int itemEnd = (int)prog.size();
          if (q == '*') {
            if (greedy)
              emit(prog, Op::Split, itemStart, (int)prog.size() + 1);
            else
              emit(prog, Op::Split, (int)prog.size() + 1, itemStart);
          } else if (q == '+') {
            if (greedy)
              emit(prog, Op::Split, itemStart, (int)prog.size() + 1);
            else
              emit(prog, Op::Split, (int)prog.size() + 1, itemStart);
          } else if (q == '?') {
            if (greedy) {
              int spIdx = (int)prog.size();
              emit(prog, Op::Split, itemStart, 0);
              prog[(usz)spIdx].y = (int)prog.size();
            } else {
              int spIdx = (int)prog.size();
              emit(prog, Op::Split, 0, itemStart);
              prog[(usz)spIdx].x = (int)prog.size();
            }
          } else if (q == '{') {
            int minVal = 0, maxVal = -1;
            while (pos < (int)p.size() && p[pos] >= '0' && p[pos] <= '9')
              minVal = minVal * 10 + (p[pos++] - '0');
            if (pos < (int)p.size() && p[pos] == ',') {
              pos++;
              if (p[pos] >= '0' && p[pos] <= '9') {
                maxVal = 0;
                while (pos < (int)p.size() && p[pos] >= '0' && p[pos] <= '9')
                  maxVal = maxVal * 10 + (p[pos++] - '0');
              }
            } else
              maxVal = minVal;
            if (pos < (int)p.size() && p[pos] == '}')
              pos++;
            Array<Inst> repeated;
            for (int r = itemStart; r < itemEnd; r++)
              repeated.push(Xi::Move(prog[(usz)r]));
            for (int r = 0; r < (itemEnd - itemStart); r++)
              prog.pop();
            for (int r = 0; r < minVal; r++)
              for (usz ri = 0; ri < repeated.size(); ri++) {
                Inst cpy = repeated[ri];
                prog.push(Xi::Move(cpy));
              }
            if (maxVal == -1) {
              int loopStart = (int)prog.size() - (int)repeated.size();
              if (greedy)
                emit(prog, Op::Split, loopStart, (int)prog.size() + 1);
              else
                emit(prog, Op::Split, (int)prog.size() + 1, loopStart);
            } else {
              for (int r = minVal; r < maxVal; r++) {
                int spIdx = (int)prog.size();
                emit(prog, Op::Split, 0, 0);
                int subStart = (int)prog.size();
                for (usz ri = 0; ri < repeated.size(); ri++) {
                  Inst cpy = repeated[ri];
                  prog.push(Xi::Move(cpy));
                }
                if (greedy) {
                  prog[(usz)spIdx].x = subStart;
                  prog[(usz)spIdx].y = (int)prog.size();
                } else {
                  prog[(usz)spIdx].x = (int)prog.size();
                  prog[(usz)spIdx].y = subStart;
                }
              }
            }
          }
        }
      }
    }
    if (pos < (int)p.size() && p[pos] == '|') {
      pos++;
      int itemEnd = (int)prog.size();
      Array<Inst> branchA;
      for (int r = coreIdx; r < itemEnd; r++)
        branchA.push(Xi::Move(prog[(usz)r]));
      for (int r = 0; r < (itemEnd - coreIdx); r++)
        prog.pop();
      int altSplitIdx = (int)prog.size();
      emit(prog, Op::Split, 0, 0);
      int startA = (int)prog.size();
      for (usz r = 0; r < branchA.size(); r++)
        prog.push(Xi::Move(branchA[r]));
      int bridgeIdx = (int)prog.size();
      emit(prog, Op::Jmp, 0);
      prog[(usz)altSplitIdx].x = startA;
      prog[(usz)altSplitIdx].y = (int)prog.size();
      compileCore(p, pos, prog, depth + 1, localIC);
      prog[(usz)bridgeIdx].x = (int)prog.size();
    }
  }

  void buildSkipTable() {
    int m = (int)prefixLiteral.size();
    for (int j = 0; j < 256; j++)
      skipTable[j] = m;
    for (int j = 0; j < m - 1; j++)
      skipTable[(u8)prefixLiteral.charAt((usz)j)] = m - 1 - j;
  }

  void compile(const String &p) {
    code = p;
    int len = (int)p.size(), i = 0;
    while (i < len && p[i] != '^' && p[i] != '$' && p[i] != '*' &&
           p[i] != '+' && p[i] != '?' && p[i] != '(' && p[i] != '[' &&
           p[i] != '\\' && p[i] != '|' && p[i] != '{') {
      prefixLiteral += p[i++];
    }
    if (!prefixLiteral.isEmpty())
      buildSkipTable();
    i = 0;
    if (len > 0 && p[i] == '^') {
      anchored = true;
      i++;
    }
    emit(inst, Op::Save, 0);
    compileCore(p, i, inst);
    emit(inst, Op::Save, 1);
    emit(inst, Op::Match);
    inst.data();
    parsed = true;
    resetDFA();
  }

  bool runVM(const String &in, int start, const Array<Inst> &prog,
             bool rev = false) const {
    struct ThreadV {
      int pc;
    };
    Array<ThreadV> cur, nxt;
    cur.push({0});
    int step = rev ? -1 : 1, limit = rev ? -1 : (int)in.size();
    for (int i = start; rev ? (i >= 0) : (i <= limit); i += step) {
      char c = (i >= 0 && i < (int)in.size()) ? in.charAt((usz)i) : 0;
      nxt = Array<ThreadV>();
      for (int t = 0; t < (int)cur.size(); ++t) {
        ThreadV th = cur[t];
        if (th.pc < 0 || th.pc >= (int)prog.size())
          continue;
        Op op = prog[(usz)th.pc].op;
        if (op == Op::Match)
          return true;
        if (op == Op::Char && c == (char)prog[(usz)th.pc].x) {
          th.pc++;
          nxt.push(th);
          continue;
        }
        if (op == Op::CharIC) {
          int tg = prog[(usz)th.pc].x;
          if (tg >= 'a' && tg <= 'z')
            tg -= 32;
          u8 uc = (u8)c;
          if (uc >= 'a' && uc <= 'z')
            uc -= 32;
          if (uc == (u8)tg && c != 0) {
            th.pc++;
            nxt.push(th);
          }
          continue;
        }
        if (op == Op::Any && (c != 0 || dotAll)) {
          th.pc++;
          nxt.push(th);
          continue;
        }
        if (op == Op::Split) {
          cur.push({prog[(usz)th.pc].x});
          cur.push({prog[(usz)th.pc].y});
          continue;
        }
        if (op == Op::Jmp) {
          cur.push({prog[(usz)th.pc].x});
          continue;
        }
        if (op == Op::Save || op == Op::AssertStart || op == Op::AssertEnd) {
          cur.push({th.pc + 1});
          continue;
        }
        if (op == Op::Class) {
          if (checkClass(prog[(usz)th.pc], (u8)c) && c != 0) {
            th.pc++;
            nxt.push(th);
          }
          continue;
        }
      }
      cur = Xi::Move(nxt);
      if (cur.size() == 0)
        break;
    }
    return false;
  }

public:
  Regex(const String &p) { compile(p); }

  Array<RegexMatch> matchAll(const String &input, int maxMatches = 0,
                             u64 limitUs = 0) const {
    Array<RegexMatch> res;
    if (maxMatches == 0)
      maxMatches = 1000000;
    struct ThreadT {
      int pc;
      long long caps[MAX_CAPS * 2];
    };
    Array<ThreadT> cur, nxt;
    int n = (int)input.size();
    int dfaState = 0;
    for (int i = 0; i <= n;) {
      char c = (i < n) ? input.charAt((usz)i) : 0;
      if (dfaState == 0 && !prefixLiteral.isEmpty() &&
          i + (int)prefixLiteral.size() <= n) {
        int k = (int)prefixLiteral.size() - 1;
        while (k >= 0 &&
               input.charAt((usz)i + (usz)k) == prefixLiteral.charAt((usz)k))
          k--;
        if (k >= 0) {
          i += skipTable[(u8)input.charAt((usz)i + prefixLiteral.size() - 1)];
          continue;
        }
      }
      if (dfaState >= 0) {
        int nextId = getNextDFAState(dfaState, (u8)c);
        if (nextId >= 0)
          dfaState = nextId;
        else
          dfaState = -1;
      }
      if (!anchored || i == 0) {
        ThreadT th;
        th.pc = 0;
        for (int j = 0; j < MAX_CAPS * 2; j++)
          th.caps[j] = -1;
        cur.push(th);
      }
      nxt = Array<ThreadT>();
      for (int t = 0; t < (int)cur.size(); ++t) {
        ThreadT th = cur[t];
        if (th.pc < 0 || th.pc >= (int)inst.size())
          continue;
        switch (inst[(usz)th.pc].op) {
        case Op::Match: {
          RegexMatch rm;
          rm.start = th.caps[0];
          rm.end = i;
          rm.full = input.substring((usz)rm.start, (usz)i);
          rm.push(rm.full);
          for (int cg = 1; cg < numCaps; cg++) {
            long long s = th.caps[cg * 2], e = th.caps[cg * 2 + 1];
            rm.push((s != -1 && e != -1) ? input.substring((usz)s, (usz)e)
                                         : String());
          }
          for (int k = 0; k < (int)capNames.size(); k++)
            if (capNames[(usz)k].idx < (int)rm.size())
              rm.namedGroups[capNames[(usz)k].name] =
                  rm[(usz)capNames[(usz)k].idx];
          bool dup = false;
          for (usz pk = 0; pk < res.size(); pk++)
            if (res[pk].start == rm.start && res[pk].end == rm.end) {
              dup = true;
              break;
            }
          if (!dup) {
            res.push(rm);
            if (res.size() >= (usz)maxMatches)
              return res;
          }
          continue;
        }
        case Op::Char:
          if (c == (char)inst[(usz)th.pc].x) {
            Array<int> set, vis;
            addEpsilon(th.pc + 1, set, vis);
            for (int pc : set) {
              ThreadT t2 = th;
              t2.pc = pc;
              nxt.push(t2);
            }
          }
          continue;
        case Op::CharIC: {
          int tg = inst[(usz)th.pc].x;
          if (tg >= 'a' && tg <= 'z')
            tg -= 32;
          u8 uc = (u8)c;
          if (uc >= 'a' && uc <= 'z')
            uc -= 32;
          if (uc == (u8)tg && c != 0) {
            Array<int> set, vis;
            addEpsilon(th.pc + 1, set, vis);
            for (int pc : set) {
              ThreadT t2 = th;
              t2.pc = pc;
              nxt.push(t2);
            }
          }
          continue;
        }
        case Op::Any:
          if (c != 0 || dotAll) {
            Array<int> set, vis;
            addEpsilon(th.pc + 1, set, vis);
            for (int pc : set) {
              ThreadT t2 = th;
              t2.pc = pc;
              nxt.push(t2);
            }
          }
          continue;
        case Op::Class: {
          if (checkClass(inst[(usz)th.pc], (u8)c) && c != 0) {
            Array<int> set, vis;
            addEpsilon(th.pc + 1, set, vis);
            for (int pc : set) {
              ThreadT t2 = th;
              t2.pc = pc;
              nxt.push(t2);
            }
          }
          continue;
        }
        case Op::Save:
          if (th.pc < (int)inst.size() && inst[(usz)th.pc].x < MAX_CAPS * 2)
            th.caps[inst[(usz)th.pc].x] = i;
          {
            Array<int> set, vis;
            addEpsilon(th.pc + 1, set, vis);
            for (int pc : set) {
              ThreadT t2 = th;
              t2.pc = pc;
              cur.push(t2);
            }
          }
          continue;
        case Op::AssertStart:
          if (i == 0) {
            Array<int> set, vis;
            addEpsilon(th.pc + 1, set, vis);
            for (int pc : set) {
              ThreadT t2 = th;
              t2.pc = pc;
              cur.push(t2);
            }
          }
          continue;
        case Op::AssertEnd:
          if (i == n) {
            Array<int> set, vis;
            addEpsilon(th.pc + 1, set, vis);
            for (int pc : set) {
              ThreadT t2 = th;
              t2.pc = pc;
              cur.push(t2);
            }
          }
          continue;
        case Op::AssertWordBound: {
          bool prev = (i > 0) && isWord(input.charAt((usz)i - 1));
          bool curr = (i < n) && isWord(input.charAt((usz)i));
          if (prev != curr) {
            Array<int> set, vis;
            addEpsilon(th.pc + 1, set, vis);
            for (int pc : set) {
              ThreadT t2 = th;
              t2.pc = pc;
              cur.push(t2);
            }
          }
        }
          continue;
        case Op::Jmp: {
          Array<int> set, vis;
          addEpsilon(inst[(usz)th.pc].x, set, vis);
          for (int pc : set) {
            ThreadT t2 = th;
            t2.pc = pc;
            cur.push(t2);
          }
        }
          continue;
        case Op::Split: {
          Array<int> s, v;
          addEpsilon(inst[(usz)th.pc].x, s, v);
          for (int pc : s) {
            ThreadT t2 = th;
            t2.pc = pc;
            cur.push(t2);
          }
          Array<int> s2, v2;
          addEpsilon(inst[(usz)th.pc].y, s2, v2);
          for (int pc : s2) {
            ThreadT t2 = th;
            t2.pc = pc;
            cur.push(t2);
          }
        }
          continue;
        case Op::Lookahead:
          if (runVM(input, i, inst[(usz)th.pc].sub)) {
            Array<int> s, v;
            addEpsilon(th.pc + 1, s, v);
            for (int pc : s) {
              ThreadT t2 = th;
              t2.pc = pc;
              cur.push(t2);
            }
          }
          continue;
        case Op::NegLookahead:
          if (!runVM(input, i, inst[(usz)th.pc].sub)) {
            Array<int> s, v;
            addEpsilon(th.pc + 1, s, v);
            for (int pc : s) {
              ThreadT t2 = th;
              t2.pc = pc;
              cur.push(t2);
            }
          }
          continue;
        case Op::Lookbehind:
          if (runVM(input, i - 1, inst[(usz)th.pc].sub, true)) {
            Array<int> s, v;
            addEpsilon(th.pc + 1, s, v);
            for (int pc : s) {
              ThreadT t2 = th;
              t2.pc = pc;
              cur.push(t2);
            }
          }
          continue;
        case Op::NegLookbehind:
          if (!runVM(input, i - 1, inst[(usz)th.pc].sub, true)) {
            Array<int> s, v;
            addEpsilon(th.pc + 1, s, v);
            for (int pc : s) {
              ThreadT t2 = th;
              t2.pc = pc;
              cur.push(t2);
            }
          }
          continue;
        default:
          continue;
        }
      }
      cur = Xi::Move(nxt);
      if (cur.size() == 0 && dfaState < 0 && (anchored || i >= n))
        break;
      i++;
      if (cur.size() == 0 && dfaState != 0)
        dfaState = 0;
    }
    return res;
  }
};

} // namespace Xi

inline Xi::Array<Xi::String> Xi::String::split(const Xi::Regex &reg) const {
  Xi::Array<Xi::String> r;
  auto m = reg.matchAll(*this);
  long long e = 0;
  for (int i = 0; i < (int)m.size(); ++i) {
    if (m[i].start > e)
      r.push(substring((usz)e, (usz)m[i].start));
    e = m[i].end;
  }
  if (e < (long long)size())
    r.push(substring((usz)e, size()));
  return r;
}

inline Xi::String Xi::String::replace(const Xi::Regex &reg,
                                      const Xi::String &rep) const {
  Xi::String r;
  auto m = reg.matchAll(*this);
  long long e = 0;
  for (int i = 0; i < (int)m.size(); ++i) {
    if (m[i].start > e)
      r += substring((usz)e, (usz)m[i].start);
    r += rep;
    e = m[i].end;
  }
  if (e < (long long)size())
    r += substring((usz)e, size());
  return r;
}

#endif
