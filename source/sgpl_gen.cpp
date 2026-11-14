// Front end: reads a signalgp-lite program and writes it as C++ that calls the
// library's real op functors, for the optimizer to reason about.
//
//   sgpl_gen ops <prog.json>             one line per instruction: kind, name
//   sgpl_gen gencpp <prog.json> <out>    the program as C++
//   sgpl_gen apply <prog.json> <mask>    the program with mask's 1s nopped
//
// kind: G global anchor, L local anchor, N nop, O anything else

#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include "peripheral.hpp"

#include "sgpl/../../third-party/cereal/include/cereal/archives/json.hpp"
#include "sgpl/../../third-party/cereal/include/cereal/types/vector.hpp"
#include "sgpl/hardware/JumpTable.hpp"
#include "sgpl/program/Program.hpp"

using spec_t = sgpl_static::spec_t;
using library_t = spec_t::library_t;
using program_t = sgpl::Program<spec_t>;
using inst_t = sgpl::Instruction<spec_t>;
using tag_t = spec_t::tag_t;
using gjt_t = sgpl::JumpTable<spec_t, spec_t::global_matching_t>;
using ljt_t = sgpl::JumpTable<spec_t, spec_t::local_matching_t>;

// NopOut runs each op once to count its RNG draws; nothing is observed then
void sgpl_static_observe(uint64_t) noexcept {}
uint64_t sgpl_static_input(uint64_t) noexcept { return 0; }

[[noreturn]] static void die(const std::string& why) {
  std::cerr << "sgpl_gen: " << why << "\n";
  std::exit(2);
}

static program_t load_program(const std::string& path) {
  std::ifstream ifs(path);
  if (!ifs) die("cannot read " + path);
  program_t prog;
  cereal::JSONInputArchive archive(ifs);
  archive(prog);
  return prog;
}

static bool is_global(const inst_t& i) {
  return library_t::IsAnchorGlobalOpCode(i.op_code);
}
static bool is_local(const inst_t& i) {
  return library_t::IsAnchorLocalOpCode(i.op_code);
}

struct Generator {
  const program_t& prog;
  const int N;
  std::array<gjt_t, 2> gjt;
  std::vector<int> anchors;
  std::vector<int> jump_dest; // positions an indexed jump may target
  bool has_local_anchors{};

  explicit Generator(const program_t& p) : prog(p), N(p.size()) {
    for (size_t j{}; j < 2; ++j) gjt[j].InitializeGlobalAnchors(
      prog, spec_t::global_jump_table_inclusion_mods[j]
    );
    for (int pos{}; pos < N; ++pos)
      if (is_global(prog[pos])) anchors.push_back(pos);
    if (anchors.empty()) die("program has no global anchor");
    for (const auto& inst : prog) has_local_anchors |= is_local(inst);
#ifdef SGPL_STATIC_JUMP_DESTS
    jump_dest = sgpl_static::jump_dests(prog);
    if ((int)jump_dest.size() != N) die("jump_dests: wrong length");
    for (const int d : jump_dest) if (d >= N) die("jump_dests: out of range");
#else
    // not announced: an indexed jump may go anywhere
    for (int pos{}; pos < N; ++pos) jump_dest.push_back(pos);
#endif
  }

  // positions of the module that starts at global anchor g: the anchor, its
  // body, and the next global anchor
  std::vector<int> module(const int g) const {
    std::vector<int> ps{g};
    for (int pos = (g + 1) % N;; pos = (pos + 1) % N) {
      ps.push_back(pos);
      if (is_global(prog[pos])) break;
    }
    return ps;
  }

  // Local anchors of the module at g that a tag matches, best first: the
  // match the engine would return if every better-ranked anchor were absent.
  // The selector takes the minimum score, first-inserted on ties, so the
  // winner among any subset of anchors is the first survivor of this chain.
  static std::string match_chain(
    program_t prog, const int g, const tag_t& tag, const bool regulated
  ) {
    std::vector<int> chain;
    for (;;) {
      ljt_t table;
      table.InitializeLocalAnchors(prog, g);
      const auto res = regulated
        ? table.MatchRegulated(tag) : table.MatchRaw(tag);
      if (!res.size()) break;
      chain.push_back(table.GetVal(res.front()));
      prog[chain.back()].NopOut();
    }
    std::string s = "-1";
    for (auto it = chain.rbegin(); it != chain.rend(); ++it)
      s = "LPICK(" + std::to_string(*it) + ", " + s + ")";
    return s;
  }

  // macro arguments for the instruction at ps[j]
  std::string args(
    const std::vector<int>& ps, const int j, const bool with_locals
  ) {
    const int m = ps.size() - 1;
    const auto& inst = prog[ps[j]];
    std::string hj = "-1", hlr = "-1";
    if (with_locals && j > 0 && j < m) {
      hj = match_chain(prog, ps[0], inst.tag, true);
      hlr = match_chain(prog, ps[0], inst.tag, false);
    }
    int hg[2] = {-1, -1};
    for (int k{}; k < 2; ++k) {
      const auto raw = gjt[k].MatchRaw(inst.tag);
      if (raw.size()) hg[k] = gjt[k].GetVal(raw.front());
    }
    std::ostringstream a;
    a << ps[j] << ", " << (int)inst.op_code << ", " << (int)inst.args[0]
      << ", " << (int)inst.args[1] << ", " << (int)inst.args[2] << ", "
      << inst.tag.GetUInt64(0) << "ULL, " << hj << ", " << hlr << ", "
      << hg[0] << ", " << hg[1] << ")\n";
    return a.str();
  }

  // one function: the module's body, up to its closing anchor
  void emit_body(
    std::ostream& o, const std::string& name, const std::vector<int>& ps,
    const std::string& has, const std::string& token, const bool with_locals
  ) {
    const int m = ps.size() - 1;
    o << "F_BEGIN(" << name << ", " << has << ", " << token << ")\n";
    for (int j = 1; j < m; ++j)
      o << "F_STEP(" << j << ", " << args(ps, j, with_locals);
    o << "F_END(" << m << ", f_A" << ps[m] << ", " << args(ps, m, with_locals)
      << "dispatch: switch (core.pc) {\n";
    if (with_locals) for (int j = 1; j < m; ++j) if (is_local(prog[ps[j]]))
      o << "  case " << ps[j] << ": goto L" << j + 1 << ";\n";
    o << "  default: __builtin_unreachable();\n}\n}\n";
  }

  // Without local anchors a global anchor never ends a core, so the program
  // is one endless loop. It becomes one function, where the optimizer can see
  // values that only feed themselves around the loop. Jumps are gotos.
  void emit_flat(std::ostream& o) {
    o << "#define SGPL_STATIC_FLAT\n#include \"sgpl_static/lower_fn.hpp\"\n"
         "static void f_flat(int target, FPARAMS) {\n"
         "  FCORE\n  switch (target) {\n"; // launch: run the anchor itself
    for (const int g : anchors) o << "  case " << g << ": goto L" << g << ";\n";
    o << "  default: return;\n  }\n";
    for (int pos{}; pos < N; ++pos)
      o << "FLAT_STEP(" << (is_global(prog[pos]) ? "FCALL_ALWAYS" : "FCALL")
        << ", " << args({pos}, 0, false);
    o << "  goto L0;\n"
         "global_jump: switch (target) {\n"; // lands just past the anchor
    for (const int g : anchors)
      o << "  case " << g << ": goto L" << (g + 1) % N << ";\n";
    o << "  default: return;\n  }\n"
         "index_jump: switch (target) {\n"; // resumes just past the target
    for (const int d : std::set<int>(jump_dest.begin(), jump_dest.end()))
      if (d >= 0) o << "  case " << d << ": goto L" << (d + 1) % N << ";\n";
    o << "  default: sgpl_static_bad_jump(target);\n  }\n}\n"
         "void f_program(int entry, LPeripheral& p) {\n"
         "  LCore core;\n"
         "  for (int r{}; r < 8; ++r) core.registers[r] = init_reg(r);\n"
         "  return f_flat(entry, FREGS, p);\n}\n";
  }

  void emit(std::ostream& o) {
    o << "#define SGPL_STATIC_PROGRAM_SIZE " << N << "\n"
      << "static constexpr int SGPL_STATIC_JUMP_DEST[] = {";
    for (const int d : jump_dest) o << d << ",";
    o << "};\n";
    if (!has_local_anchors) return emit_flat(o);
    o << "#include \"sgpl_static/lower_fn.hpp\"\n"
      << "static void f_gdispatch(int target, FPARAMS);\n";

    std::ostringstream body;
    std::map<int, bool> has_locals;
    for (const int g : anchors) {
      const auto ps = module(g);
      const int m = ps.size() - 1;
      // Each local anchor is present unless its switch says otherwise, so
      // "has any anchors" and the table's identity token are expressions.
      std::ostringstream has, token;
      has << "(0";
      token << "((" << (1469598103934665603ULL ^ (uint64_t)g) << "ULL";
      for (int j = 1; j < m; ++j) if (is_local(prog[ps[j]])) {
        has_locals[g] = true;
        has << " || LON(" << ps[j] << ")";
        token << " ^ LTOK(" << ps[j] << ", "
          << ((uint64_t)(ps[j] + 1) * 0x9E3779B97F4A7C15ULL) << "ULL)";
      }
      has << ")";
      token << ") | 1)";
      const std::string id = std::to_string(g);
      // launched core: runs the anchor, then the body with local anchors
      o << "F_DECL(f_L" << id << ")\nF_DECL(f_A" << id << ")\n";
      body << "F_LAUNCH(f_L" << id << ", f_A" << id << ", "
        << args(ps, 0, false);
      emit_body(body, "f_A" + id, ps, has.str(), token.str(), has_locals[g]);
      // a global jump lands just past the anchor, with no local anchors
      if (has_locals[g]) {
        o << "F_DECL(f_B" << id << ")\n";
        emit_body(body, "f_B" + id, ps, "0", "0ULL", false);
      }
    }
    o << body.str();

    // a global jump may land in any module (regulated matching)
    o << "static void f_gdispatch(int target, FPARAMS) {\n"
         "  FCORE\n  switch (target) {\n";
    for (const int g : anchors)
      o << "  case " << g << ": return f_" << (has_locals[g] ? "B" : "A") << g
        << "(FREGS, p);\n";
    o << "  default: return;\n}\n}\n";

    // a core may be launched at any global anchor
    o << "void f_program(int entry, LPeripheral& p) {\n"
         "  LCore core;\n"
         "  for (int r{}; r < 8; ++r) core.registers[r] = init_reg(r);\n"
         "  switch (entry) {\n";
    for (const int g : anchors)
      o << "  case " << g << ": return f_L" << g << "(FREGS, p);\n";
    o << "  default: return;\n}\n}\n";
  }
};

int main(int argc, char** argv) {
  if (argc < 3) die("usage: sgpl_gen ops|gencpp|apply <prog.json> [arg]");
  const std::string mode = argv[1];
  program_t prog = load_program(argv[2]);
  if (mode == "ops") {
    for (const auto& inst : prog) {
      const std::string name = inst.GetOpName();
      std::cout << (
        is_global(inst) ? 'G' : is_local(inst) ? 'L'
        : name.rfind("Nop-", 0) == 0 ? 'N' : 'O'
      ) << '\t' << name << '\n';
    }
  } else if (mode == "gencpp" && argc == 4) {
    std::ofstream out(argv[3]);
    Generator(prog).emit(out);
  } else if (mode == "apply" && argc == 4) {
    const std::string mask = argv[3];
    if (mask.size() != prog.size()) die("mask length differs from program");
    for (size_t i{}; i < prog.size(); ++i) {
      if (mask[i] != '1') continue;
      if (is_global(prog[i])) die("mask nops a global anchor");
      prog[i].NopOut();
    }
    cereal::JSONOutputArchive archive(std::cout);
    archive(prog);
  } else die("bad arguments");
  return 0;
}
