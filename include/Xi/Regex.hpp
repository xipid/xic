#ifndef REGEX_HPP
#define REGEX_HPP

#include "Array.hpp"
#include "Map.hpp"
#include "Primitives.hpp"
#include "String.hpp"
#include "Time.hpp"

namespace Xi {

struct RegexMatch : public Array<String> {
  String full;
  long long start = -1;
  long long end = -1;
  Map<String, String> namedGroups;
  RegexMatch() = default;
};

class Regex {
public:
  bool parsed = false;
  String code;
  bool ignoreCase = false;
  bool multiLine = false;
  bool dotAll = false;
  bool sticky = false;

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
    int x;
    int y;
    bool invert;
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

  // Boyer-Moore / Horspool Skip Table
  String prefixLiteral;
  int skipTable[256];

  void buildSkipTable() {
    int len = prefixLiteral.size();
    for (int i = 0; i < 256; i++)
      skipTable[i] = len;
    for (int i = 0; i < len - 1; i++)
      skipTable[(u8)prefixLiteral.charAt(i)] = len - 1 - i;
  }

  void emit(Array<Inst> &p, Op op, int x = 0, int y = 0) {
    Inst i;
    i.op = op;
    i.x = x;
    i.y = y;
    i.invert = false;
    p.push(i);
  }

  void compileCore(const String &p, int &pos, Array<Inst> &prog) {
    int startIdx = prog.size();
    while (pos < (int)p.size() && p.charAt(pos) != ')') {
      char c = p.charAt(pos++);
      if (c == '^')
        emit(prog, Op::AssertStart);
      else if (c == '$')
        emit(prog, Op::AssertEnd);
      else if (c == '.')
        emit(prog, Op::Any);
      else if (c == '\\' && pos < (int)p.size()) {
        char nc = p.charAt(pos++);
        if (nc == 'w' || nc == 'd' || nc == 's' || nc == 'b') {
          Inst ci;
          ci.op = (nc == 'b') ? Op::AssertWordBound : Op::Class;
          ci.chars += nc;
          prog.push(ci);
        } else
          emit(prog, Op::Char, (int)nc);
      } else if (c == '(') {
        bool cap = true;
        String name = "";
        if (pos < (int)p.size() && p.charAt(pos) == '?') {
          pos++;
          if (p.charAt(pos) == 'P' || p.charAt(pos) == '<') {
            if (p.charAt(pos) == 'P')
              pos++;
            if (p.charAt(pos) == '<') {
              pos++;
              while (pos < (int)p.size() && p.charAt(pos) != '>')
                name += p.charAt(pos++);
              if (pos < (int)p.size())
                pos++;
            }
          } else if (p.charAt(pos) == ':') {
            cap = false;
            pos++;
          } else if (p.charAt(pos) == '=') {
            pos++;
            emit(prog, Op::Lookahead);
            cap = false;
            prog[prog.size() - 1].sub = compileSub(p, pos);
          } else if (p.charAt(pos) == '!') {
            pos++;
            emit(prog, Op::NegLookahead);
            cap = false;
            prog[prog.size() - 1].sub = compileSub(p, pos);
          } else if (p.charAt(pos) == '<' && pos + 1 < (int)p.size()) {
            pos++;
            char t = p.charAt(pos++);
            if (t == '=') {
              emit(prog, Op::Lookbehind);
              prog[prog.size() - 1].sub = compileSub(p, pos);
            } else if (t == '!') {
              emit(prog, Op::NegLookbehind);
              prog[prog.size() - 1].sub = compileSub(p, pos);
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
        compileCore(p, pos, prog);
        if (cap)
          emit(prog, Op::Save, idx * 2 + 1);
        if (pos < (int)p.size() && p.charAt(pos) == ')')
          pos++;
      } else if (c == '*' || c == '+' || c == '?') {
        int last = prog.size() - 1;
        if (last >= startIdx) {
          if (c == '*')
            emit(prog, Op::Split, last, prog.size() + 1);
          else if (c == '+')
            emit(prog, Op::Split, last, prog.size() + 1);
          else if (c == '?')
            emit(prog, Op::Split, last, prog.size() + 1); // Approximation
        }
      } else
        emit(prog, Op::Char, (int)c);
    }
  }

  Array<Inst> compileSub(const String &p, int &pos) {
    Array<Inst> s;
    compileCore(p, pos, s);
    emit(s, Op::Match);
    return s;
  }

  void compile(const String &p) {
    code = p;
    int len = p.size(), i = 0;
    while (i < len && p[i] != '^' && p[i] != '$' && p[i] != '*' &&
           p[i] != '+' && p[i] != '?' && p[i] != '(' && p[i] != '[' &&
           p[i] != '\\' && p[i] != '|') {
      prefixLiteral += p[i++];
    }
    if (!prefixLiteral.isEmpty())
      buildSkipTable();
    i = 0;
    if (len == 0 || p.charAt(0) != '^') {
      int sp = inst.size();
      emit(inst, Op::Split, sp + 1, 0);
      emit(inst, Op::Any);
      emit(inst, Op::Jmp, sp);
      inst[sp].y = inst.size();
    }
    emit(inst, Op::Save, 0);
    compileCore(p, i, inst);
    emit(inst, Op::Save, 1);
    emit(inst, Op::Match);
    parsed = true;
  }

  bool runVM(const String &in, int start, const Array<Inst> &prog,
             bool rev = false) const {
    struct Thread {
      int pc;
    };
    Array<Thread> cur, nxt;
    cur.push({0});
    int step = rev ? -1 : 1, limit = rev ? -1 : (int)in.size();
    for (int i = start; rev ? (i >= -1) : (i <= limit); i += step) {
      char c = (i >= 0 && i < (int)in.size()) ? in.charAt(i) : 0;
      nxt = Array<Thread>();
      for (int t = 0; t < cur.size(); ++t) {
        Thread th = cur[t];
        if (th.pc >= (int)prog.size())
          continue;
        Op op = prog[th.pc].op;
        if (op == Op::Match)
          return true;
        if (op == Op::Char && c == prog[th.pc].x) {
          th.pc++;
          nxt.push(th);
        } else if (op == Op::Any && c != 0) {
          th.pc++;
          nxt.push(th);
        } else if (op == Op::Split) {
          cur.push({prog[th.pc].x});
          cur.push({prog[th.pc].y});
        } else if (op == Op::Jmp)
          cur.push({prog[th.pc].x});
      }
      cur = nxt;
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
    u64 startT = limitUs > 0 ? micros() : 0;
    struct Thread {
      int pc;
      long long caps[20];
    };
    Array<Thread> cur, nxt;
    cur.push({0, {-1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
                  -1, -1, -1, -1, -1, -1, -1, -1, -1, -1}});

    int n = input.size();
    for (int i = 0; i <= n;) {
      if (limitUs > 0 && (micros() - startT) > limitUs)
        break;
      if (cur.size() == 1 && cur[0].pc == 0 && !prefixLiteral.isEmpty() &&
          i + (int)prefixLiteral.size() <= n) {
        int k = (int)prefixLiteral.size() - 1;
        while (k >= 0 && input.charAt(i + k) == prefixLiteral.charAt(k))
          k--;
        if (k >= 0) {
          i += skipTable[(u8)input.charAt(i + (int)prefixLiteral.size() - 1)];
          continue;
        }
      }
      char c = (i < n) ? input.charAt(i) : 0;
      nxt = Array<Thread>();
      for (int t = 0; t < cur.size(); ++t) {
        Thread th = cur[t];
        if (th.pc < 0 || th.pc >= inst.size())
          continue;
#if defined(__GNUC__) || defined(__clang__)
        static void *ls[] = {&&o_m,  &&o_c,  &&o_ci, &&o_a,  &&o_cl,
                             &&o_j,  &&o_sp, &&o_sv, &&o_as, &&o_ae,
                             &&o_aw, &&o_la, &&o_nl, &&o_lb, &&o_nb};
        goto *ls[(int)inst[th.pc].op];
#else
        switch (inst[th.pc].op) {
        case Op::Match:
          goto o_m;
        case Op::Char:
          goto o_c;
        case Op::Any:
          goto o_a;
        case Op::Save:
          goto o_sv;
        case Op::AssertStart:
          goto o_as;
        case Op::AssertEnd:
          goto o_ae;
        case Op::Jmp:
          goto o_j;
        case Op::Split:
          goto o_sp;
        case Op::Lookahead:
          goto o_la;
        case Op::NegLookahead:
          goto o_nl;
        case Op::Lookbehind:
          goto o_lb;
        case Op::NegLookbehind:
          goto o_nb;
        default:
          continue;
        }
#endif
      o_m: {
        RegexMatch rm;
        rm.start = th.caps[0];
        rm.end = i;
        rm.full = input.substring(rm.start, i);
        rm.push(rm.full);
        for (int cg = 1; cg < numCaps; cg++) {
          long long s = th.caps[cg * 2], e = th.caps[cg * 2 + 1];
          rm.push((s != -1 && e != -1) ? input.substring(s, e) : String());
        }
        for (int j = 0; j < capNames.size(); j++)
          rm.namedGroups[capNames[j].name] = rm[capNames[j].idx];
        res.push(rm);
        if (res.size() >= (usz)maxMatches)
          return res;
        continue;
      }
      o_c:
        if (c == inst[th.pc].x) {
          th.pc++;
          nxt.push(th);
        }
        continue;
      o_ci: {
        char tg = (char)inst[th.pc].x;
        if (c == tg || (c >= 'a' && c <= 'z' && c - 32 == tg) ||
            (c >= 'A' && c <= 'Z' && c + 32 == tg)) {
          th.pc++;
          nxt.push(th);
        }
        continue;
      }
      o_a:
        if (c != 0) {
          th.pc++;
          nxt.push(th);
        }
        continue;
      o_cl: {
        char ty = inst[th.pc].chars.charAt(0);
        bool ok =
            (ty == 'd' && c >= '0' && c <= '9') ||
            (ty == 'w' && ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                           (c >= '0' && c <= '9') || c == '_')) ||
            (ty == 's' && (c == ' ' || c == '\t' || c == '\r' || c == '\n'));
        if (ok) {
          th.pc++;
          nxt.push(th);
        }
        continue;
      }
      o_sv:
        if (inst[th.pc].x < 20)
          th.caps[inst[th.pc].x] = i;
        th.pc++;
        cur.push(th);
        continue;
      o_as:
        if (i == 0) {
          th.pc++;
          cur.push(th);
        }
        continue;
      o_ae:
        if (i == n) {
          th.pc++;
          cur.push(th);
        }
        continue;
      o_j:
        th.pc = inst[th.pc].x;
        cur.push(th);
        continue;
      o_sp: {
        Thread st = th;
        st.pc = inst[th.pc].y;
        th.pc = inst[th.pc].x;
        cur.push(th);
        cur.push(st);
      }
        continue;
      o_aw:
        continue;
      o_la:
        if (runVM(input, i, inst[th.pc].sub)) {
          th.pc++;
          cur.push(th);
        }
        continue;
      o_nl:
        if (!runVM(input, i, inst[th.pc].sub)) {
          th.pc++;
          cur.push(th);
        }
        continue;
      o_lb:
        if (runVM(input, i - 1, inst[th.pc].sub, true)) {
          th.pc++;
          cur.push(th);
        }
        continue;
      o_nb:
        if (!runVM(input, i - 1, inst[th.pc].sub, true)) {
          th.pc++;
          cur.push(th);
        }
        continue;
      }
      cur = nxt;
      i++;
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
