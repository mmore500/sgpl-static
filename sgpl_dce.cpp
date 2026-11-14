// Prototype: lower signalgp-lite programs to LLVM IR for dead-instruction
// detection, and differentially validate nop masks in the real sgpl engine.
//
// usage:
//   sgpl_dce ops <prog.json>
//   sgpl_dce lower <prog.json>            (stdin lines: "<mask|-> <outpath>")
//   sgpl_dce validate <prog.json> <mask> <ntrials> <nupdates>
//
// env INIT_ZERO=1 : assume host never seeds registers of launched cores

#include <array>
#include <bit>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <map>
#include <random>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include "sgpl/algorithm/execute_cpu_n_slices.hpp"
#include "sgpl/hardware/Cpu.hpp"
#include "sgpl/library/OpLibraryCoupler.hpp"
#include "sgpl/library/prefab/CompleteOpLibrary.hpp"
#include "sgpl/operations/operations.hpp"
#include "sgpl/program/Program.hpp"
#include "sgpl/spec/Spec.hpp"
#include "sgpl/utility/ThreadLocalRandom.hpp"

#include "mock_spec.hpp"

void Peripheral::Log(const uint64_t v) { log.push_back(v); }

static program_t load_program(const std::string& path) {
  std::ifstream ifs(path);
  std::stringstream ss;
  ss << ifs.rdbuf();
  return program_t(ss.str().c_str());
}

static program_t apply_mask(program_t prog, const std::string& mask) {
  if (mask == "-") return prog;
  if (mask.size() != prog.size()) {
    std::cerr << "bad mask size\n";
    std::exit(2);
  }
  for (size_t i{}; i < prog.size(); ++i)
    if (mask[i] == '1') prog[i].NopOut();
  return prog;
}

// ---------------------------------------------------------------------------
// lowering
// ---------------------------------------------------------------------------
static bool ends_with(const std::string& s, const std::string& suf) {
  return s.size() >= suf.size()
    && s.compare(s.size() - suf.size(), suf.size(), suf) == 0;
}

static std::string fconst(const float f) {
  char buf[32];
  std::snprintf(
    buf, sizeof buf, "0x%016llX",
    (unsigned long long)std::bit_cast<uint64_t>((double)f)
  );
  return buf;
}

struct Lowerer {
  const program_t& prog;
  const int N;
  const bool init_zero;
  std::vector<std::string> names;
  std::vector<int> globals;
  std::array<gjt_t, 2> gjt;
  std::array<bool, 2> gdyn{};
  std::map<int, core_t> lcores;          // per module: core with local table
  std::map<int, std::vector<int>> lscope; // per module: local anchor positions
  std::map<int, bool> ldyn;              // per module: regulated matching?
  std::map<std::pair<int, int>, std::string> blocks;
  std::vector<std::pair<int, int>> work;
  int tmp{};

  Lowerer(const program_t& p, const bool iz)
  : prog(p), N(p.size()), init_zero(iz) {
    for (const auto& inst : prog) names.push_back(inst.GetOpName());
    for (int i{}; i < N; ++i)
      if (names[i] == "Global Anchor") globals.push_back(i);
    for (size_t j{}; j < 2; ++j) gjt[j].InitializeGlobalAnchors(
      prog, spec_t::global_jump_table_inclusion_mods[j]
    );
    // global matching is dynamic if any regulator writer has a raw match
    for (int i{}; i < N; ++i) {
      const int jt = gjt_idx(names[i]);
      if (jt < 0 || is_get(names[i])) continue;
      if (gjt[jt].MatchRaw(prog[i].tag).size()) gdyn[jt] = true;
    }
    for (const int g : globals) {
      auto& core = lcores[g];
      core.JumpToIndex(g);
      core.LoadLocalAnchors(prog);
      auto& scope = lscope[g];
      bool dyn = false;
      for (int pos = (g + 1) % N; pos != g; pos = (pos + 1) % N) {
        if (names[pos] == "Global Anchor") break;
        if (names[pos] == "Local Anchor") scope.push_back(pos);
        if (
          is_lreg(names[pos]) && !is_get(names[pos])
          && core.GetLocalJumpTable().MatchRaw(prog[pos].tag).size()
        ) dyn = true;
      }
      ldyn[g] = dyn;
    }
  }

  static bool is_get(const std::string& n) { return n.rfind("Get ", 0) == 0; }
  static bool is_lreg(const std::string& n) {
    return ends_with(n, "Local Regulator");
  }
  static int gjt_idx(const std::string& n) {
    if (ends_with(n, "Secondary Global Regulator")) return 1;
    if (ends_with(n, "Global Regulator")) return 0;
    return -1;
  }

  std::string label(const int pc, const int g) {
    const auto key = std::make_pair(pc, g);
    if (!blocks.count(key)) { blocks[key] = ""; work.push_back(key); }
    return "%B" + std::to_string(pc) + "_"
      + (g < 0 ? std::string("n") : std::to_string(g));
  }
  std::string T() { return "%t" + std::to_string(tmp++); }

  std::string load(std::ostream& o, const int r) {
    const auto t = T();
    o << "  " << t << " = load float, ptr %r" << r << "\n";
    return t;
  }
  void store(std::ostream& o, const int r, const std::string& v) {
    o << "  store float " << v << ", ptr %r" << r << "\n";
  }
  std::string truthy(std::ostream& o, const int r, const bool negate=false) {
    const auto v = load(o, r);
    const auto t = T();
    o << "  " << t << " = fcmp " << (negate ? "oeq" : "une") << " float "
      << v << ", 0.0\n";
    return t;
  }
  std::string b2f(std::ostream& o, const std::string& b) {
    const auto t = T();
    o << "  " << t << " = uitofp i1 " << b << " to float\n";
    return t;
  }
  std::string f2i(std::ostream& o, const std::string& f) {
    const auto t = T();
    o << "  " << t << " = bitcast float " << f << " to i32\n";
    return t;
  }
  std::string i2f(std::ostream& o, const std::string& i) {
    const auto t = T();
    o << "  " << t << " = bitcast i32 " << i << " to float\n";
    return t;
  }

  void emit_block(const int pc, const int g) {
    std::ostringstream o;
    const auto& inst = prog[pc];
    const auto& nm = names[pc];
    const int a = inst.args[0], b = inst.args[1], c = inst.args[2];
    const int nxt = (pc + 1) % N;
    const std::string self = label(pc, g).substr(1);
    o << self << ":\n  call void @tick(i32 " << pc << ")\n";
    bool done = false; // terminator already emitted?

    auto binop = [&](const char* op) {
      const auto x = load(o, b), y = load(o, c);
      const auto t = T();
      o << "  " << t << " = " << op << " float " << x << ", " << y << "\n";
      store(o, a, t);
    };
    auto cmpop = [&](const char* pred) {
      const auto x = load(o, b), y = load(o, c);
      const auto t = T();
      o << "  " << t << " = fcmp " << pred << " float " << x << ", " << y
        << "\n";
      store(o, a, b2f(o, t));
    };
    auto bitop = [&](const char* op) {
      const auto x = f2i(o, load(o, b)), y = f2i(o, load(o, c));
      const auto t = T();
      o << "  " << t << " = " << op << " i32 " << x << ", " << y << "\n";
      store(o, a, i2f(o, t));
    };
    auto purecall2 = [&](const char* fn) {
      const auto x = load(o, b), y = load(o, c);
      const auto t = T();
      o << "  " << t << " = call float @" << fn << "(float " << x
        << ", float " << y << ")\n";
      store(o, a, t);
    };
    auto input = [&](const std::string& call) {
      const auto t = T();
      o << "  " << t << " = call float " << call << "\n";
      store(o, a, t);
    };

    if (nm == "Global Anchor") {
      if (g >= 0) { o << "  br label %term\n"; done = true; }
      else {
        const int g2 = lscope[pc].empty() ? -1 : pc;
        if (g2 >= 0) o << "  store i64 " << pc + 1 << ", ptr %ljt\n";
        o << "  br label " << label(nxt, g2) << "\n";
        done = true;
      }
    } else if (nm == "Local Anchor" || nm.rfind("Nop-", 0) == 0) {
      ;
    } else if (nm == "Local Jump If" || nm == "Local Jump If Not") {
      const bool neg = nm == "Local Jump If Not";
      if (g < 0) { ; } // empty local jump table: jump is a no-op
      else if (!ldyn[g]) {
        auto core = lcores[g]; // copy
        core.JumpToLocalAnchorMatch(inst.tag);
        const int dest = core.GetProgramCounter();
        if (dest != g) {
          const auto cnd = truthy(o, a, neg);
          o << "  br i1 " << cnd << ", label " << label((dest + 1) % N, g)
            << ", label " << label(nxt, g) << "\n";
          done = true;
        }
      } else {
        const auto cnd = truthy(o, a, neg);
        o << "  br i1 " << cnd << ", label %" << self << ".j, label "
          << label(nxt, g) << "\n" << self << ".j:\n";
        const auto tok = T(), m = T();
        o << "  " << tok << " = load i64, ptr %ljt\n";
        o << "  " << m << " = call i32 @lmatch(i64 " << tok << ", i32 " << pc
          << ")\n";
        o << "  switch i32 " << m << ", label " << label(nxt, g) << " [\n";
        for (const int t : lscope[g])
          o << "    i32 " << t << ", label " << label((t + 1) % N, g) << "\n";
        o << "  ]\n";
        done = true;
      }
    } else if (nm == "Global Jump If" || nm == "Global Jump If Not") {
      const bool neg = nm == "Global Jump If Not";
      const auto cnd = truthy(o, a, neg);
      o << "  br i1 " << cnd << ", label %" << self << ".j, label "
        << label(nxt, g) << "\n" << self << ".j:\n";
      std::string m;
      int sdest = -1;
      if (gdyn[0]) {
        m = T();
        o << "  " << m << " = call i32 @gmatch(i32 " << pc << ")\n";
      } else {
        const auto res = gjt[0].MatchRegulated(inst.tag);
        if (res.size()) sdest = gjt[0].GetVal(res.front());
      }
      const auto rc = truthy(o, b, neg);
      for (int r{}; r < NREG; ++r) {
        const auto v = load(o, r);
        const auto t = T();
        o << "  " << t << " = select i1 " << rc << ", float 0.0, float " << v
          << "\n";
        store(o, r, t);
      }
      if (gdyn[0]) {
        o << "  switch i32 " << m << ", label %term [\n";
        for (const int t : globals)
          o << "    i32 " << t << ", label " << label((t + 1) % N, -1) << "\n";
        o << "  ]\n";
      } else if (sdest >= 0) {
        o << "  br label " << label((sdest + 1) % N, -1) << "\n";
      } else o << "  br label %term\n";
      done = true;
    } else if (nm == "Terminate If") {
      const auto cnd = truthy(o, a);
      o << "  br i1 " << cnd << ", label %term, label " << label(nxt, g)
        << "\n";
      done = true;
    } else if (nm == "Fork If") {
      const auto cnd = truthy(o, a);
      o << "  br i1 " << cnd << ", label %" << self << ".f, label "
        << label(nxt, g) << "\n" << self << ".f:\n";
      o << "  call void @fork(i32 " << pc << ")\n";
    }
    else if (nm == "Add") binop("fadd");
    else if (nm == "Subtract") binop("fsub");
    else if (nm == "Multiply") binop("fmul");
    else if (nm == "Divide") binop("fdiv");
    else if (nm == "Modulo") purecall2("op_modulo");
    else if (nm == "Bitwise Shift") purecall2("op_bitshift");
    else if (nm == "Equal") cmpop("oeq");
    else if (nm == "Not Equal") cmpop("une");
    else if (nm == "Less Than") cmpop("olt");
    else if (nm == "Greater Than") cmpop("ogt");
    else if (nm == "Logical And" || nm == "Logical Or") {
      const auto x = truthy(o, b), y = truthy(o, c);
      const auto t = T();
      o << "  " << t << " = " << (nm == "Logical And" ? "and" : "or")
        << " i1 " << x << ", " << y << "\n";
      store(o, a, b2f(o, t));
    }
    else if (nm == "Bitwise And") bitop("and");
    else if (nm == "Bitwise Or") bitop("or");
    else if (nm == "Bitwise Xor") bitop("xor");
    else if (nm == "Bitwise Not") {
      const auto x = f2i(o, load(o, b));
      const auto t = T();
      o << "  " << t << " = xor i32 " << x << ", -1\n";
      store(o, a, i2f(o, t));
    } else if (nm == "Count Ones") {
      const auto x = f2i(o, load(o, b));
      const auto t = T(), u = T();
      o << "  " << t << " = call i32 @llvm.ctpop.i32(i32 " << x << ")\n";
      o << "  " << u << " = uitofp i32 " << t << " to float\n";
      store(o, a, u);
    } else if (nm == "Increment" || nm == "Decrement") {
      const auto x = load(o, a);
      const auto t = T();
      o << "  " << t << " = " << (nm == "Increment" ? "fadd" : "fsub")
        << " float " << x << ", 1.0\n";
      store(o, a, t);
    } else if (nm == "Negate") {
      const auto x = load(o, a);
      const auto t = T();
      o << "  " << t << " = fneg float " << x << "\n";
      store(o, a, t);
    } else if (nm == "Not") {
      store(o, a, b2f(o, truthy(o, a, true)));
    } else if (nm == "Terminal") {
      core_t scratch;
      Peripheral p;
      sgpl::Terminal::run<spec_t>(scratch, inst, prog, p);
      store(o, a, fconst(scratch.registers[a]));
    }
    else if (nm == "Generate Random Bool")
      input("@rand_bool(i32 " + std::to_string(pc) + ")");
    else if (nm == "Draw Random Value") input("@rand_draw()");
    else if (nm == "RandomFill") input("@rand_fill()");
    else if (nm == "Read Own State")
      input("@read_own(i32 " + std::to_string(raddr(inst)) + ")");
    else if (nm == "Read Neighbor State")
      input("@read_nbr(i32 " + std::to_string(naddr(inst)) + ")");
    else if (nm == "Add To Own State" || nm == "Multiply Own State") {
      const auto x = load(o, a);
      o << "  call void @" << (nm[0] == 'A' ? "add_own" : "mul_own")
        << "(i32 " << waddr(inst) << ", float " << x << ")\n";
    } else if (nm == "Write Own State") {
      const auto cnd = truthy(o, b);
      o << "  br i1 " << cnd << ", label %" << self << ".w, label "
        << label(nxt, g) << "\n" << self << ".w:\n";
      const auto x = load(o, a);
      o << "  call void @write_own(i32 " << waddr(inst) << ", float " << x
        << ")\n";
    } else if (
      nm == "Send Inter-Cell Message" || nm == "Send Intra-Cell Message"
      || nm == "Broadcast Intra-Cell Message"
    ) {
      const auto cnd = truthy(o, a);
      o << "  br i1 " << cnd << ", label %" << self << ".s, label "
        << label(nxt, g) << "\n" << self << ".s:\n";
      std::vector<std::string> vs;
      for (int r{}; r < NREG; ++r) vs.push_back(load(o, r));
      o << "  call void @send(i32 " << pc;
      for (const auto& v : vs) o << ", float " << v;
      o << ")\n";
    } else if (is_lreg(nm)) {
      // empty table or no raw match: op does nothing
      if (
        g >= 0
        && lcores[g].GetLocalJumpTable().MatchRaw(inst.tag).size()
      ) {
        const auto tok = T();
        o << "  " << tok << " = load i64, ptr %ljt\n";
        if (is_get(nm)) {
          // which anchor's regulator is read is part of the op's identity
          auto& ljt = lcores[g].GetLocalJumpTable();
          const int src = ljt.GetVal(ljt.MatchRaw(inst.tag).front());
          input(
            "@lreg_get(i64 " + tok + ", i32 " + std::to_string(pc) + ", i32 "
            + std::to_string(src) + ")"
          );
        } else {
          const auto x = load(o, a);
          const auto t = T();
          o << "  " << t << " = call i64 @lreg_mod(i64 " << tok << ", i32 "
            << pc << ", float " << x << ")\n";
          o << "  store i64 " << t << ", ptr %ljt\n";
        }
      }
    } else if (gjt_idx(nm) >= 0) {
      if (gjt[gjt_idx(nm)].MatchRaw(inst.tag).size()) {
        if (is_get(nm)) {
          auto& jt = gjt[gjt_idx(nm)];
          const int src = jt.GetVal(jt.MatchRaw(inst.tag).front());
          input(
            "@greg_get(i32 " + std::to_string(pc) + ", i32 "
            + std::to_string(src) + ")"
          );
        }
        else {
          const auto x = load(o, a);
          o << "  call void @greg_mod(i32 " << pc << ", float " << x << ")\n";
        }
      }
    } else {
      std::cerr << "unhandled op: " << nm << "\n";
      std::exit(3);
    }
    if (!done) o << "  br label " << label(nxt, g) << "\n";
    blocks[{pc, g}] = o.str();
  }

  std::string run() {
    std::ostringstream o;
    o << "declare void @tick(i32)\n"
         "declare void @fork(i32)\n"
         "declare void @send(i32, float, float, float, float, float, float, "
         "float, float)\n"
         "declare void @write_own(i32, float)\n"
         "declare void @add_own(i32, float)\n"
         "declare void @mul_own(i32, float)\n"
         "declare void @greg_mod(i32, float)\n"
         "declare float @init_reg(i32) #0\n"
         "declare float @read_own(i32) #0\n"
         "declare float @read_nbr(i32) #0\n"
         "declare float @rand_bool(i32) #0\n"
         "declare float @rand_draw() #0\n"
         "declare float @rand_fill() #0\n"
         "declare float @greg_get(i32, i32) #0\n"
         "declare float @lreg_get(i64, i32, i32) #0\n"
         "declare i32 @gmatch(i32) #0\n"
         "declare i32 @lmatch(i64, i32) #0\n"
         "declare i64 @lreg_mod(i64, i32, float) #1\n"
         "declare float @op_modulo(float, float) #1\n"
         "declare float @op_bitshift(float, float) #1\n"
         "declare i32 @llvm.ctpop.i32(i32)\n"
         "attributes #0 = { nounwind willreturn memory(read) }\n"
         "attributes #1 = { nounwind willreturn memory(none) }\n\n"
         "define void @core(i32 %e) {\nentry:\n";
    for (int r{}; r < NREG; ++r) o << "  %r" << r << " = alloca float\n";
    o << "  %ljt = alloca i64\n  store i64 0, ptr %ljt\n";
    for (int r{}; r < NREG; ++r) {
      if (init_zero) o << "  store float 0.0, ptr %r" << r << "\n";
      else {
        o << "  %i" << r << " = call float @init_reg(i32 " << r << ")\n";
        o << "  store float %i" << r << ", ptr %r" << r << "\n";
      }
    }
    o << "  switch i32 %e, label %term [\n";
    for (const int g : globals)
      o << "    i32 " << g << ", label " << label(g, -1) << "\n";
    o << "  ]\nterm:\n  ret void\n";
    while (work.size()) {
      const auto [pc, g] = work.back();
      work.pop_back();
      emit_block(pc, g);
    }
    for (const auto& [k, v] : blocks) o << v;
    o << "}\n";
    return o.str();
  }
};

// ---------------------------------------------------------------------------
// per-module C++ generation: real functor calls in program order
// ---------------------------------------------------------------------------
struct ModuleGen {
  const program_t& base;
  const int N;
  std::array<gjt_t, 2> gjt; // global anchors are never nopped: build once
  // registers live on entry to each module, by entry mode (default: all)
  std::map<int, unsigned> live_launch, live_jump;
  unsigned live_at(const std::map<int, unsigned>& m, const int g) const {
    const auto it = m.find(g);
    return it == m.end() ? 0xFFu : it->second;
  }

  explicit ModuleGen(const program_t& p) : base(p), N(p.size()) {
    for (size_t j{}; j < 2; ++j) gjt[j].InitializeGlobalAnchors(
      base, spec_t::global_jump_table_inclusion_mods[j]
    );
  }

  static bool is_global(const inst_t& i) {
    return library_t::IsAnchorGlobalOpCode(i.op_code);
  }
  static bool is_local(const inst_t& i) {
    return library_t::IsAnchorLocalOpCode(i.op_code);
  }

  void emit(
    std::ostream& o, const std::string& id, const int g,
    const std::vector<int>& nopped
  ) {
    program_t prog = base;
    for (const int i : nopped) prog[i].NopOut();
    std::vector<int> ps{g};
    for (int pos = (g + 1) % N;; pos = (pos + 1) % N) {
      ps.push_back(pos);
      if (is_global(prog[pos])) break;
    }
    const int m = ps.size() - 1;
    std::vector<int> locals;
    uint64_t token = 1469598103934665603ULL ^ (uint64_t)g;
    for (int j = 1; j < m; ++j) if (is_local(prog[ps[j]])) {
      locals.push_back(j);
      token = (token ^ (uint64_t)ps[j]) * 1099511628211ULL;
    }
    token |= 1;
    core_t lc;
    lc.JumpToIndex(g);
    lc.LoadLocalAnchors(prog);
    auto& ljt = lc.GetLocalJumpTable();

    for (const bool via_jump : {false, true}) {
      if (via_jump && locals.empty()) continue; // identical to launch mode
      // a global jump may land in any module (regulated matching)
      unsigned jump_live = 0;
      for (int pos{}; pos < N; ++pos) if (is_global(base[pos]))
        jump_live |= live_at(live_jump, pos);
      o << "MODULE_BEGIN(f_" << id << (via_jump ? "_B" : "_A") << ", "
        << live_at(live_launch, ps[m]) << ", " << jump_live << ")\n";
      for (int j = via_jump ? 1 : 0; j <= m; ++j) {
        const auto& inst = prog[ps[j]];
        int hj = -1, hlr = -1, hg[2] = {-1, -1};
        if (!via_jump && j > 0 && j < m) {
          const auto reg = ljt.MatchRegulated(inst.tag);
          if (reg.size()) hj = ljt.GetVal(reg.front());
          const auto raw = ljt.MatchRaw(inst.tag);
          if (raw.size()) hlr = ljt.GetVal(raw.front());
        }
        for (int k{}; k < 2; ++k) {
          const auto raw = gjt[k].MatchRaw(inst.tag);
          if (raw.size()) hg[k] = gjt[k].GetVal(raw.front());
        }
        if (j == 0) o << "STEP_ENTRY(0, " << !locals.empty() << ", " << token
          << "ULL, ";
        else o << (j == m ? "STEP_END(" : "STEP(") << j << ", ";
        o << ps[j] << ", " << (int)inst.op_code << ", " << (int)inst.args[0]
          << ", " << (int)inst.args[1] << ", " << (int)inst.args[2] << ", "
          << inst.tag.GetUInt64(0) << "ULL, " << hj << ", " << hlr << ", "
          << hg[0] << ", " << hg[1] << ")\n";
      }
      o << "dispatch: switch (core.pc) {\n";
      if (!via_jump) for (const int j : locals)
        o << "  case " << ps[j] << ": goto L" << j + 1 << ";\n";
      o << "  default: __builtin_unreachable();\n}\n}\n";
    }
  }

  // Which registers does the instruction at pos write? Found by running the
  // real op on a scratch core with several register fills. Only used to
  // place diagnostic markers, so a missed conditional write is harmless.
  static unsigned probe_writes(
    const program_t& prog, const int pos, const int module_anchor,
    const bool via_jump
  ) {
    static Peripheral scratch_peripheral;
    unsigned mask = 0;
    for (int fill{}; fill < 4; ++fill) {
      sgpl::Cpu<spec_t> cpu;
      cpu.InitializeAnchors(prog);
      cpu.TryLaunchCore();
      auto& core = cpu.GetFreshestCore();
      // a core that arrived by global jump has no local anchors loaded
      core.JumpToIndex(module_anchor);
      if (!via_jump) core.LoadLocalAnchors(prog);
      core_t::registers_t regs;
      for (int r{}; r < NREG; ++r) regs[r] = fill == 0 ? 1000.5f + r
        : fill == 1 ? 0.0f : fill == 2 ? 1.0f : -0.37f * (7 + r);
      core.SetRegisters(regs);
      core.JumpToIndex(pos);
      sgpl::advance_core<spec_t>(core, prog, scratch_peripheral);
      for (int r{}; r < NREG; ++r)
        if (fbits(core.registers[r]) != fbits(regs[r])) mask |= 1u << r;
    }
    return mask;
  }

  // whole program as one function: a block per (module, entry mode)
  void emit_whole(
    std::ostream& o, const std::string& id, const std::vector<int>& nopped
  ) {
    program_t prog = base;
    for (const int i : nopped) prog[i].NopOut();
    std::vector<int> anchors;
    for (int pos{}; pos < N; ++pos)
      if (is_global(prog[pos])) anchors.push_back(pos);
    std::map<int, bool> has_locals;
    std::ostringstream body;
    for (const int g : anchors) {
      std::vector<int> ps{g};
      for (int pos = (g + 1) % N;; pos = (pos + 1) % N) {
        ps.push_back(pos);
        if (is_global(prog[pos])) break;
      }
      const int m = ps.size() - 1;
      std::vector<int> locals;
      uint64_t token = 1469598103934665603ULL ^ (uint64_t)g;
      for (int j = 1; j < m; ++j) if (is_local(prog[ps[j]])) {
        locals.push_back(j);
        token = (token ^ (uint64_t)ps[j]) * 1099511628211ULL;
      }
      token |= 1;
      has_locals[g] = !locals.empty();
      core_t lc;
      lc.JumpToIndex(g);
      lc.LoadLocalAnchors(prog);
      auto& ljt = lc.GetLocalJumpTable();
      for (const bool via_jump : {false, true}) {
        if (via_jump && locals.empty()) continue;
        const std::string mid = std::to_string(g) + (via_jump ? "B" : "A");
        auto args = [&](const int j) {
          const auto& inst = prog[ps[j]];
          int hj = -1, hlr = -1, hg[2] = {-1, -1};
          std::string raw_list; // every local anchor this tag raw-matches
          if (!via_jump && j > 0 && j < m) {
            const auto reg = ljt.MatchRegulated(inst.tag);
            if (reg.size()) hj = ljt.GetVal(reg.front());
            const auto raw = ljt.MatchRaw(inst.tag);
            if (raw.size()) hlr = ljt.GetVal(raw.front());
            for (const auto uid : raw)
              raw_list += "," + std::to_string(ljt.GetVal(uid));
          }
          for (int k{}; k < 2; ++k) {
            const auto raw = gjt[k].MatchRaw(inst.tag);
            if (raw.size()) hg[k] = gjt[k].GetVal(raw.front());
          }
          std::ostringstream a;
          a << ps[j] << ", " << (int)inst.op_code << ", " << (int)inst.args[0]
            << ", " << (int)inst.args[1] << ", " << (int)inst.args[2] << ", "
            << inst.tag.GetUInt64(0) << "ULL, " << hj << ", " << hlr << ", "
            << hg[0] << ", " << hg[1] << ", "
            << (is_global(inst) ? 0u : probe_writes(prog, ps[j], g, via_jump))
            << ") // raw=" << raw_list << " pos=" << ps[j] << "\n";
          return a.str();
        };
        if (!via_jump) body << "W_ENTRY(" << mid << ", " << args(0);
        body << "W_BODY(" << mid << ", " << (via_jump ? 0 : !locals.empty())
          << ", " << (via_jump ? 0 : token) << "ULL)\n";
        for (int j = 1; j < m; ++j)
          body << "W_STEP(" << mid << ", " << j << ", " << args(j);
        body << "W_END(" << mid << ", " << m << ", " << ps[m] << "A, "
          << args(m);
        body << "D" << mid << ": switch (core.pc) {\n";
        if (!via_jump) for (const int j : locals)
          body << "  case " << ps[j] << ": ARRIVE(" << ps[j] << ") goto L"
            << mid << "_" << j + 1 << ";\n";
        body << "  default: __builtin_unreachable();\n}\n";
      }
    }
    o << "PROGRAM_BEGIN(f_" << id << ")\nswitch (entry) {\n";
    for (const int g : anchors)
      o << "  case " << g << ": goto E" << g << "A;\n";
    o << "  default: return;\n}\n" << body.str();
    // a global jump may land in any module (regulated matching); it arrives
    // just past the anchor with no local anchors loaded
    o << "gdispatch: switch (gm) {\n";
    for (const int g : anchors)
      o << "  case " << g << ": goto M" << g << (has_locals[g] ? "B" : "A")
        << ";\n";
    o << "  default: return;\n}\n}\n";
  }

  // Local anchors of the module at g that a tag matches, best first: the
  // match the engine would return if every better-ranked anchor were absent.
  // The selector takes the minimum score, first-inserted on ties, so the
  // winner among any subset of anchors is the first survivor of this chain.
  static std::vector<int> match_chain(
    program_t prog, const int g, const tag_t& tag, const bool regulated
  ) {
    std::vector<int> chain;
    for (;;) {
      core_t lc;
      lc.JumpToIndex(g);
      lc.LoadLocalAnchors(prog);
      auto& ljt = lc.GetLocalJumpTable();
      const auto res = regulated ? ljt.MatchRegulated(tag) : ljt.MatchRaw(tag);
      if (!res.size()) break;
      const int dest = ljt.GetVal(res.front());
      chain.push_back(dest);
      prog[dest].NopOut();
    }
    return chain;
  }
  static std::string pick(const std::vector<int>& chain) {
    std::string s = "-1";
    for (auto it = chain.rbegin(); it != chain.rend(); ++it)
      s = "LPICK(" + std::to_string(*it) + ", " + s + ")";
    return s;
  }

  // a function per (module, entry mode); exits are direct calls
  void emit_functions(
    std::ostream& o, const std::string& id, const std::vector<int>& nopped
  ) {
    program_t prog = base;
    for (const int i : nopped) prog[i].NopOut();
    std::vector<int> anchors;
    for (int pos{}; pos < N; ++pos)
      if (is_global(prog[pos])) anchors.push_back(pos);
    std::map<int, bool> has_locals;
    std::ostringstream body, decls;
    for (const int g : anchors) {
      std::vector<int> ps{g};
      for (int pos = (g + 1) % N;; pos = (pos + 1) % N) {
        ps.push_back(pos);
        if (is_global(prog[pos])) break;
      }
      const int m = ps.size() - 1;
      // Each local anchor is present unless its switch says otherwise, so
      // "has any anchors" and the table's identity token are expressions.
      std::vector<int> locals;
      std::ostringstream has, token;
      has << "(0";
      token << "((" << (1469598103934665603ULL ^ (uint64_t)g) << "ULL";
      for (int j = 1; j < m; ++j) if (is_local(prog[ps[j]])) {
        locals.push_back(j);
        has << " || LON(" << ps[j] << ")";
        token << " ^ LTOK(" << ps[j] << ", "
          << ((uint64_t)(ps[j] + 1) * 0x9E3779B97F4A7C15ULL) << "ULL)";
      }
      has << ")";
      token << ") | 1)";
      has_locals[g] = !locals.empty();
      for (const bool via_jump : {false, true}) {
        if (via_jump && locals.empty()) continue;
        decls << "F_DECL(f_" << (via_jump ? "B" : "A") << g << ")\n";
        if (!via_jump) decls << "F_DECL(f_L" << g << ")\n";
        auto args = [&](const int j) {
          const auto& inst = prog[ps[j]];
          std::string hj = "-1", hlr = "-1";
          int hg[2] = {-1, -1};
          if (!via_jump && j > 0 && j < m && !locals.empty()) {
            hj = pick(match_chain(prog, g, inst.tag, true));
            hlr = pick(match_chain(prog, g, inst.tag, false));
          }
          for (int k{}; k < 2; ++k) {
            const auto raw = gjt[k].MatchRaw(inst.tag);
            if (raw.size()) hg[k] = gjt[k].GetVal(raw.front());
          }
          std::ostringstream a;
          a << ps[j] << ", " << (int)inst.op_code << ", " << (int)inst.args[0]
            << ", " << (int)inst.args[1] << ", " << (int)inst.args[2] << ", "
            << inst.tag.GetUInt64(0) << "ULL, " << hj << ", " << hlr << ", "
            << hg[0] << ", " << hg[1] << ", "
            << (is_global(inst) ? 0u : probe_writes(prog, ps[j], g, via_jump))
            << ")\n";
          return a.str();
        };
        const std::string name = std::string("f_") + (via_jump ? "B" : "A")
          + std::to_string(g);
        if (!via_jump)
          body << "F_LAUNCH(f_L" << g << ", " << name << ", " << args(0);
        body << "F_BEGIN(" << name << ", " << (via_jump ? "0" : has.str())
          << ", " << (via_jump ? "0ULL" : token.str()) << ")\n";
        for (int j = 1; j < m; ++j) body << "F_STEP(" << j << ", " << args(j);
        body << "F_END(" << m << ", f_A" << ps[m] << ", " << args(m);
        body << "dispatch: switch (core.pc) {\n";
        if (!via_jump) for (const int j : locals)
          body << "  case " << ps[j] << ": ARRIVE(" << ps[j] << ") goto L"
            << j + 1 << ";\n";
        body << "  default: __builtin_unreachable();\n}\n}\n";
      }
    }
    o << "static void f_gdispatch(int target, FPARAMS);\n" << decls.str()
      << body.str();
    // a global jump may land in any module (regulated matching); it arrives
    // just past the anchor with no local anchors loaded
    o << "static void f_gdispatch(int target, FPARAMS) {\n"
         "  FCORE\n  switch (target) {\n";
    for (const int g : anchors)
      o << "  case " << g << ": return f_" << (has_locals[g] ? "B" : "A") << g
        << "(FREGS, p);\n";
    o << "  default: return;\n}\n}\n";
    o << "void f_program(int entry, Peripheral& p) {\n"
         "  LCore core;\n"
         "  for (int r{}; r < 8; ++r) core.registers[r] = init_reg(r);\n"
         "  switch (entry) {\n";
    for (const int g : anchors)
      o << "  case " << g << ": return f_L" << g << "(FREGS, p);\n";
    o << "  default: return;\n}\n}\n";
  }
};

// ---------------------------------------------------------------------------
// differential validation in the real engine
// ---------------------------------------------------------------------------
static std::vector<char> coverage;
static void run_slices(
  const size_t max_slices, sgpl::Cpu<spec_t>& cpu, const program_t& prog,
  Peripheral& p
) {
  if (coverage.empty()) {
    sgpl::execute_cpu_n_slices<spec_t>(max_slices, cpu, prog, p);
    return;
  }
  // same loop as execute_cpu_n_slices, recording executed positions
  for (size_t s{}; s < max_slices && cpu.HasActiveCore(); ++s) {
    auto& core = cpu.GetActiveCore();
    size_t c{};
    for (; c < spec_t::switch_steps && !core.HasTerminated(); ++c) {
      coverage[core.GetProgramCounter()] = 1;
      sgpl::advance_core<spec_t>(core, prog, p);
    }
    cpu.AdvanceCycleClock(c);
    if (core.HasTerminated()) cpu.KillActiveCore();
    cpu.TryActivateNextCore();
  }
}

static std::vector<uint64_t> trace_run(
  const program_t& prog, const program_t& tagsrc, const int seed,
  const int nupdates
) {
  sgpl::tlrand.Reseed(seed);
  std::mt19937_64 host(seed * 7919 + 13);
  auto rnd = [&](const uint64_t n) { return host() % n; };
  auto randval = [&]() -> float {
    switch (rnd(6)) {
      case 0: return 0.0f;
      case 1: return 1.0f;
      case 2: return (float)rnd(8) - 3.0f;
      case 3: return std::bit_cast<float>((uint32_t)host());
      default: return (float)((double)rnd(2000001) / 1e6 - 1.0);
    }
  };
  std::vector<tag_t> tags;
  for (const auto& inst : tagsrc) {
    const auto nm = inst.GetOpName();
    if (
      nm == "Global Anchor" || nm.find("Message") != std::string::npos
      || nm == "Fork If"
    ) tags.push_back(inst.tag);
  }
  auto randtag = [&]() {
    tag_t t = tags.empty() ? tag_t{} : tags[rnd(tags.size())];
    if (rnd(2)) for (int k = rnd(6); k > 0; --k) t.Toggle(rnd(64));
    return t;
  };

  sgpl::Cpu<spec_t> cpu;
  cpu.InitializeAnchors(prog);
  Peripheral p;
  for (auto& v : p.own) v = randval();
  for (auto& v : p.nbr) v = randval();
  std::vector<uint64_t> trace;
  for (int u{}; u < nupdates; ++u) {
    for (size_t i = Peripheral::NW; i < Peripheral::NR; ++i)
      if (rnd(4) == 0) p.own[i] = randval();
    for (auto& v : p.nbr) if (rnd(4) == 0) v = randval();
    for (int k = rnd(3); k > 0; --k) cpu.ForceLaunchCore(randtag());
    for (int k = rnd(3); k > 0; --k) {
      const auto tag = randtag();
      const size_t jt = rnd(2);
      core_t::registers_t regs;
      for (auto& r : regs) r = randval();
      if (cpu.TryLaunchCore(tag, jt)) cpu.GetFreshestCore().SetRegisters(regs);
    }
    run_slices(1 + rnd(24), cpu, prog, p);
    if (rnd(3) == 0) cpu.DecayGlobalRegulators();
    trace.push_back(0xFEED0000 + u);
    trace.push_back(cpu.GetCyclesSinceConstruction());
    trace.push_back(cpu.GetNumBusyCores());
    for (size_t i{}; i < cpu.GetNumBusyCores(); ++i)
      trace.push_back(cpu.GetCore(i).GetProgramCounter());
    for (const float v : p.own) trace.push_back(fbits(v));
    trace.insert(trace.end(), p.log.begin(), p.log.end());
    p.log.clear();
  }
  return trace;
}

int main(int argc, char** argv) {
  if (argc < 3) { std::cerr << "usage\n"; return 2; }
  const std::string mode = argv[1];
  const program_t prog = load_program(argv[2]);
  const bool init_zero = std::getenv("INIT_ZERO") != nullptr;
  if (mode == "ops") {
    for (const auto& inst : prog) std::cout << inst.GetOpName() << "\n";
  } else if (mode == "lower") {
    std::string mask, out;
    while (std::cin >> mask >> out) {
      const auto variant = apply_mask(prog, mask);
      std::ofstream(out) << Lowerer(variant, init_zero).run();
      std::cout << out << std::endl;
    }
  } else if (mode == "gencpp") {
    // stdin: "FILE <path>" | "FN <id> <anchor> <pos,pos,...|->" | "END"
    //        | "LIVE <anchor> <launch-mode mask> <jump-mode mask>"
    ModuleGen gen(prog);
    std::ofstream out;
    std::string cmd, path;
    while (std::cin >> cmd) {
      if (cmd == "FILE") {
        std::cin >> path;
        out.open(path);
        out << "#include \"lower_fn.hpp\"\n";
      } else if (cmd == "FN") {
        std::string id, list;
        int g;
        std::cin >> id >> g >> list;
        std::vector<int> nopped;
        if (list != "-") {
          std::stringstream ss(list);
          for (std::string tok; std::getline(ss, tok, ',');)
            nopped.push_back(std::stoi(tok));
        }
        if (g == -2) gen.emit_functions(out, id, nopped);
        else if (g < 0) gen.emit_whole(out, id, nopped); // whole program
        else gen.emit(out, id, g, nopped);
      } else if (cmd == "SKIP") {
        // a full copy of the program in its own namespace, in which only
        // position `pos` may be skipped (-2: every position)
        std::string id;
        int pos;
        std::cin >> id >> pos;
        out << "#undef SKIP_ONE\n#define SKIP_ONE (" << pos << ")\n"
            << "namespace v" << id << " {\n";
        gen.emit_functions(out, id, {});
        out << "}\n";
      } else if (cmd == "SKIPSET") {
        // a full program copy in which a given set of positions is skippable
        std::string id, list;
        std::cin >> id >> list;
        std::vector<char> table(prog.size(), 0);
        std::stringstream ss(list);
        for (std::string tok; std::getline(ss, tok, ',');)
          table[std::stoi(tok)] = 1;
        out << "static constexpr unsigned char skiptab_" << id << "[] = {";
        for (const char c : table) out << (int)c << ",";
        out << "};\n#undef SKIP_PRED\n#define SKIP_PRED(POS) (skiptab_" << id
            << "[POS])\nnamespace v" << id << " {\n";
        gen.emit_functions(out, id, {});
        out << "}\n";
      } else if (cmd == "LIVE") {
        int g;
        unsigned launch, jump;
        std::cin >> g >> launch >> jump;
        gen.live_launch[g] = launch;
        gen.live_jump[g] = jump;
      } else if (cmd == "END") {
        out.close();
        std::cout << path << std::endl;
      }
    }
  } else if (mode == "dump") {
    const auto variant = apply_mask(prog, argv[3]);
    cereal::JSONOutputArchive archive(std::cout);
    archive(variant);
  } else if (mode == "coverage") {
    coverage.assign(prog.size(), 1);
    std::vector<char> seen(prog.size(), 0);
    coverage.assign(prog.size(), 0);
    for (int s = 1; s <= std::atoi(argv[3]); ++s)
      trace_run(prog, prog, s, std::atoi(argv[4]));
    for (const char c : coverage) std::cout << (c ? '1' : '0');
    std::cout << "\n";
  } else if (mode == "validate") {
    const auto variant = apply_mask(prog, argv[3]);
    const int ntrials = std::atoi(argv[4]), nupdates = std::atoi(argv[5]);
    size_t nfail{}, total_events{}, total_cycles{};
    for (int s = 1; s <= ntrials; ++s) {
      const auto t0 = trace_run(prog, prog, s, nupdates);
      const auto t1 = trace_run(variant, prog, s, nupdates);
      total_events += t0.size();
      if (t0 != t1) {
        ++nfail;
        size_t i{};
        while (i < t0.size() && i < t1.size() && t0[i] == t1[i]) ++i;
        std::cout << "MISMATCH seed " << s << " at event " << i << " of "
          << t0.size() << "\n";
      }
      // cycles are recorded right after each update marker; take the last
      for (size_t i = t0.size(); i-- > 0;) if (t0[i] >> 16 == 0xFEED) {
        total_cycles += t0[i + 1];
        break;
      }
    }
    std::cout << "trials " << ntrials << " failures " << nfail
      << " trace_events " << total_events << " cycles_executed "
      << total_cycles << "\n";
    return nfail ? 1 : 0;
  }
  return 0;
}
