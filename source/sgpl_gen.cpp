// Front end: reads a signalgp-lite program and writes it as C++ that calls the
// library's real op functors, for the optimizer to reason about.
//
//   sgpl_gen ops <prog.json>             one character per instruction: kind
//   sgpl_gen gencpp <prog.json> <out>    the program as C++
//   sgpl_gen apply <prog.json> <mask>    the program with mask's 1s nopped
//
// kind: G global anchor, L local anchor, N nop, O anything else

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <algorithm>
#include <array>
#include <fstream>
#include <iostream>
#include <numeric>
#include <set>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

#include "peripheral.hpp"

#include "sgpl/../../third-party/cereal/include/cereal/archives/json.hpp"
#include "sgpl/../../third-party/cereal/include/cereal/types/vector.hpp"
#include "sgpl/hardware/JumpTable.hpp"
#include "sgpl/program/Program.hpp"

using spec_t    = sgpl_static::spec_t;
using library_t = spec_t::library_t;
using program_t = sgpl::Program<spec_t>;
using inst_t    = sgpl::Instruction<spec_t>;
using tag_t     = spec_t::tag_t;
using gjt_t     = sgpl::JumpTable<spec_t, spec_t::global_matching_t>;
using ljt_t     = sgpl::JumpTable<spec_t, spec_t::local_matching_t>;

// The generated code assumes a jump table lookup yields at most one match:
// a selector S<Threshold, 1>, which signalgp-lite's RankedSelector is by
// default.
template <typename Depository>
struct selector_of;

template <template <typename, typename, typename, typename, auto...> class Depository,
          typename Val,
          typename Metric,
          typename Selector,
          typename Regulator,
          auto... Caching>
struct selector_of<Depository<Val, Metric, Selector, Regulator, Caching...>> {
  using type = Selector;
};

template <typename Selector>
struct picks_one : std::false_type {};

template <template <typename, size_t> class S, typename Threshold>
struct picks_one<S<Threshold, 1>> : std::true_type {};

static_assert(picks_one<selector_of<spec_t::global_matching_t>::type>::value &&
                picks_one<selector_of<spec_t::local_matching_t>::type>::value,
              "jump table selectors must pick exactly one match");

// NopOut runs each op once to count its RNG draws; nothing is observed then
void sgpl_static_observe(uint64_t) noexcept {}

uint64_t sgpl_static_input(uint64_t) noexcept { return 0; }

[[noreturn]] static void die(const std::string & why) {
  std::fprintf(stderr, "sgpl_gen: %s\n", why.c_str());
  std::exit(2);
}

static program_t load_program(const std::string & path) {
  std::ifstream ifs(path);
  if (!ifs) { die("cannot read " + path); }
  program_t prog;
  cereal::JSONInputArchive archive(ifs);
  archive(prog);
  return prog;
}

static bool is_global(const inst_t & i) { return library_t::IsAnchorGlobalOpCode(i.op_code); }

static bool is_local(const inst_t & i) { return library_t::IsAnchorLocalOpCode(i.op_code); }

// printf-style formatting into a string
template <typename... Args>
static std::string fmt(const char * format, const Args... args) {
  std::string text(std::snprintf(nullptr, 0, format, args...), '\0');
  std::snprintf(text.data(), text.size() + 1, format, args...);
  return text;
}

struct Generator {
  const program_t & prog;
  const int N;
  std::array<gjt_t, 2> gjt;
  std::vector<int> anchors;
  std::vector<int> jump_dest;  // positions an indexed jump may target
  const bool has_local_anchors;

  explicit Generator(const program_t & p)
    : prog(p)
    , N(static_cast<int>(p.size()))
    , has_local_anchors(std::any_of(p.begin(), p.end(), is_local)) {
    for (size_t j{}; j < gjt.size(); ++j) {
      gjt[j].InitializeGlobalAnchors(prog, spec_t::global_jump_table_inclusion_mods[j]);
    }
    for (int pos{}; pos < N; ++pos) {
      if (is_global(prog[pos])) { anchors.push_back(pos); }
    }
    if (anchors.empty()) { die("program has no global anchor"); }
#ifdef SGPL_STATIC_JUMP_DESTS
    jump_dest = sgpl_static::jump_dests(prog);
    if (jump_dest.size() != prog.size()) { die("jump_dests: wrong length"); }
    if (std::any_of(jump_dest.begin(), jump_dest.end(), [this](const int d) { return d >= N; })) {
      die("jump_dests: out of range");
    }
#else
    // not announced: an indexed jump may go anywhere
    jump_dest.resize(prog.size());
    std::iota(jump_dest.begin(), jump_dest.end(), 0);
#endif
  }

  // positions of the module that starts at global anchor g: the anchor, its
  // body, and the next global anchor
  std::vector<int> module(const int g) const {
    std::vector<int> ps{g};
    for (int pos = (g + 1) % N;; pos = (pos + 1) % N) {
      ps.push_back(pos);
      if (is_global(prog[pos])) { break; }
    }
    return ps;
  }

  // Local anchors of the module at g that a tag matches, best first: the
  // match the engine would return if every better-ranked anchor were absent.
  // The selector takes the minimum score, first-inserted on ties, so the
  // winner among any subset of anchors is the first survivor of this chain.
  static std::string match_chain(program_t prog,
                                 const int g,
                                 const tag_t & tag,
                                 const bool regulated) {
    std::vector<int> chain;
    for (;;) {
      ljt_t table;
      table.InitializeLocalAnchors(prog, g);
      const auto res = regulated ? table.MatchRegulated(tag) : table.MatchRaw(tag);
      if (!res.size()) { break; }
      chain.push_back(table.GetVal(res.front()));
      prog[chain.back()].NopOut();
    }
    return std::accumulate(chain.rbegin(),
                           chain.rend(),
                           std::string("-1"),
                           [](const std::string & picks, const int anchor) {
                             return fmt("LPICK(%d, %s)", anchor, picks.c_str());
                           });
  }

  // macro arguments for the instruction at ps[j]
  std::string args(const std::vector<int> & ps, const int j, const bool with_locals) {
    const auto & inst  = prog[ps[j]];
    const bool in_body = j > 0 && j + 1 < static_cast<int>(ps.size());
    const auto chain   = [&](const bool regulated) {
      return with_locals && in_body ? match_chain(prog, ps[0], inst.tag, regulated)
                                      : std::string("-1");
    };
    // the global anchor each table's raw match picks, or -1
    const auto global_raw = [&](auto & table) {
      const auto raw = table.MatchRaw(inst.tag);
      return raw.size() ? static_cast<int>(table.GetVal(raw.front())) : -1;
    };
    // what the instruction becomes when nopped; a global anchor never is
    inst_t nopped = inst;
    if (!is_global(inst)) { nopped.NopOut(); }
    return fmt("%d, %d, %d, %d, %d, %d, %lluULL, %s, %s, %d, %d)\n",
               ps[j],
               int{inst.op_code},
               int{nopped.op_code},
               int{inst.args[0]},
               int{inst.args[1]},
               int{inst.args[2]},
               static_cast<unsigned long long>(inst.tag.GetUInt64(0)),
               chain(true).c_str(),
               chain(false).c_str(),
               global_raw(gjt[0]),
               global_raw(gjt[1]));
  }

  // one function: the module's body, up to its closing anchor
  std::string body(const std::string & name,
                   const std::vector<int> & ps,
                   const std::string & has,
                   const std::string & token,
                   const bool with_locals) {
    const int m = static_cast<int>(ps.size()) - 1;
    auto out    = fmt("F_BEGIN(%s, %s, %s)\n", name.c_str(), has.c_str(), token.c_str());
    for (int j = 1; j < m; ++j) { out += fmt("F_STEP(%d, ", j) + args(ps, j, with_locals); }
    out += fmt("F_END(%d, f_A%d, ", m, ps[m]) + args(ps, m, with_locals);
    out += "dispatch: switch (core.pc) {\n";
    if (with_locals) {
      for (int j = 1; j < m; ++j) {
        if (is_local(prog[ps[j]])) { out += fmt("  case %d: goto L%d;\n", ps[j], j + 1); }
      }
    }
    return out + "  default: __builtin_unreachable();\n}\n}\n";
  }

  // Without local anchors a global anchor never ends a core, so the program
  // is one endless loop. It becomes one function, where the optimizer can see
  // values that only feed themselves around the loop. Jumps are gotos.
  std::string flat() {
    std::string out = R"(#define SGPL_STATIC_FLAT
#include "sgpl_static/lower_fn.hpp"
static void f_flat(int target, FPARAMS) {
  FCORE
  switch (target) {
)";
    // launch: run the anchor itself
    for (const int g : anchors) { out += fmt("  case %d: goto L%d;\n", g, g); }
    out += "  default: return;\n  }\n";
    for (int pos{}; pos < N; ++pos) {
      out += fmt("FLAT_STEP(%s, ", is_global(prog[pos]) ? "FCALL_ALWAYS" : "FCALL") +
             args({pos}, 0, false);
    }
    // a global jump lands just past the anchor
    out += "  goto L0;\nglobal_jump: switch (target) {\n";
    for (const int g : anchors) { out += fmt("  case %d: goto L%d;\n", g, (g + 1) % N); }
    // an indexed jump resumes just past its target
    out += "  default: return;\n  }\nindex_jump: switch (target) {\n";
    for (const int d : std::set<int>(jump_dest.begin(), jump_dest.end())) {
      if (d >= 0) { out += fmt("  case %d: goto L%d;\n", d, (d + 1) % N); }
    }
    return out + R"(  default: sgpl_static_bad_jump(target);
  }
}
void f_program(int entry, LPeripheral& p) {
  LCore core;
  for (int r{}; r < 8; ++r) core.registers[r] = init_reg(r);
  return f_flat(entry, FREGS, p);
}
)";
  }

  // a function per module and entry mode; fall-through and jumps are calls
  std::string modules() {
    std::string decls, bodies, jumps, launches;
    for (const int g : anchors) {
      const auto ps = module(g);
      // Each local anchor is present unless its switch says otherwise, so
      // "has any anchors" and the table's identity token are expressions.
      std::string has = "(0";
      std::string token =
        fmt("((%lluULL", 1'469'598'103'934'665'603ULL ^ static_cast<unsigned long long>(g));
      // the body: every position but the two anchors at the ends
      for (auto pos = ps.begin() + 1; pos + 1 != ps.end(); ++pos) {
        if (!is_local(prog[*pos])) { continue; }
        has += fmt(" || LON(%d)", *pos);
        token += fmt(" ^ LTOK(%d, %lluULL)",
                     *pos,
                     static_cast<unsigned long long>(*pos + 1) * 0x9E37'79B9'7F4A'7C15ULL);
      }
      const bool has_locals = has != "(0";
      has += ")";
      token += ") | 1)";
      // launched core: runs the anchor, then the body with local anchors
      decls += fmt("F_DECL(f_L%d)\nF_DECL(f_A%d)\n", g, g);
      bodies += fmt("F_LAUNCH(f_L%d, f_A%d, ", g, g) + args(ps, 0, false);
      bodies += body(fmt("f_A%d", g), ps, has, token, has_locals);
      // a global jump lands just past the anchor, with no local anchors
      if (has_locals) {
        decls += fmt("F_DECL(f_B%d)\n", g);
        bodies += body(fmt("f_B%d", g), ps, "0", "0ULL", false);
      }
      jumps += fmt("  case %d: return f_%c%d(FREGS, p);\n", g, has_locals ? 'B' : 'A', g);
      launches += fmt("  case %d: return f_L%d(FREGS, p);\n", g, g);
    }
    // a global jump may land in any module (regulated matching), and a core
    // may be launched at any global anchor
    return R"(#include "sgpl_static/lower_fn.hpp"
static void f_gdispatch(int target, FPARAMS);
)" + decls +
           bodies + R"(static void f_gdispatch(int target, FPARAMS) {
  FCORE
  switch (target) {
)" + jumps +
           R"(  default: return;
}
}
void f_program(int entry, LPeripheral& p) {
  LCore core;
  for (int r{}; r < 8; ++r) core.registers[r] = init_reg(r);
  switch (entry) {
)" + launches +
           "  default: return;\n}\n}\n";
  }

  std::string program() {
    auto out = fmt("#define SGPL_STATIC_PROGRAM_SIZE %d\n"
                   "static constexpr int SGPL_STATIC_JUMP_DEST[] = {",
                   N);
    for (const int d : jump_dest) { out += fmt("%d,", d); }
    return out + "};\n" + (has_local_anchors ? modules() : flat());
  }
};

int main(const int argc, char ** argv) {
  if (argc < 3) { die("usage: sgpl_gen ops|gencpp|apply <prog.json> [arg]"); }
  const std::string_view mode = argv[1];
  program_t prog              = load_program(argv[2]);
  if (mode == "ops") {
    for (const auto & inst : prog) {
      std::putchar(is_global(inst)                          ? 'G'
                   : is_local(inst)                         ? 'L'
                   : inst.GetOpName().rfind("Nop-", 0) == 0 ? 'N'
                                                            : 'O');
    }
  } else if (mode == "gencpp" && argc == 4) {
    std::ofstream(argv[3]) << Generator(prog).program();
  } else if (mode == "apply" && argc == 4) {
    const std::string_view mask = argv[3];
    if (mask.size() != prog.size()) { die("mask length differs from program"); }
    for (size_t i{}; i < prog.size(); ++i) {
      if (mask[i] != '1') { continue; }
      if (is_global(prog[i])) { die("mask nops a global anchor"); }
      prog[i].NopOut();
    }
    cereal::JSONOutputArchive archive(std::cout);
    archive(prog);
  } else {
    die("bad arguments");
  }
}
