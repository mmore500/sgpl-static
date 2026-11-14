#pragma once
// Module-per-function form. Each module is a function whose parameters are
// the eight registers; fall-through and global jumps are direct calls. The
// compiler's dead-argument elimination then works out which registers each
// module actually needs from its predecessors.

// --- Analysis-only RNG, with no library change -----------------------------
// Pre-defining the include guard skips sgpl's ThreadLocalRandom.hpp, and the
// macro makes every `sgpl::tlrand` an RNG that lives in a temporary, seeded
// from an opaque input. The real engine code still runs on it, but on local
// state, so a draw whose result is unused leaves nothing behind.
#define SGPL_UTILITY_THREADLOCALRANDOM_HPP_INCLUDE
#include <array>
#include <climits>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <ctime>
#include <iostream>
#include <limits>
#include <string>
#include <vector>
#include "sgpl/../../third-party/conduit/include/uit_emp/math/Random.hpp"
extern "C" {
__attribute__((pure)) uint64_t rand_state(int site, int word) noexcept;
}
namespace sgpl {
struct AnalysisRng {
  uit_emp::Random rand{1};
  // Every word of the engine's state is an opaque input. Seeding through the
  // constructor is NOT enough: it takes a 31-bit seed, and the first draw
  // from a small seed is nearly constant, which the optimizer can exploit.
  explicit AnalysisRng(const int site) {
    uint64_t words[sizeof(rand) / sizeof(uint64_t)];
    static_assert(sizeof(words) == sizeof(rand));
    for (int w{}; w < int(sizeof(words) / sizeof(uint64_t)); ++w)
      words[w] = rand_state(site, w);
    std::memcpy(static_cast<void*>(&rand), words, sizeof(rand));
  }
  uit_emp::Random& Get() { return rand; }
  std::byte GetByte() { return std::byte(rand.GetUInt(256)); }
};
} // namespace sgpl
#define tlrand AnalysisRng{__COUNTER__}
// ---------------------------------------------------------------------------

#include "lower_whole.hpp"

#define FPARAMS \
  float r0, float r1, float r2, float r3, float r4, float r5, float r6, \
  float r7, Peripheral& p
#define FREGS \
  core.registers[0], core.registers[1], core.registers[2], core.registers[3], \
  core.registers[4], core.registers[5], core.registers[6], core.registers[7]
#define FCORE \
  LCore core; \
  core.registers = {r0, r1, r2, r3, r4, r5, r6, r7}; \
  core.global[0].jt = 0; core.global[1].jt = 1;

// -DSGPL_NOINLINE_MODULES keeps every module a separate function
#ifdef SGPL_NOINLINE_MODULES
#define FATTR __attribute__((noinline))
#else
#define FATTR
#endif
#define F_DECL(NAME) FATTR static void NAME(FPARAMS);

// force the real op body inline, but leave module-to-module calls to clang
template<size_t OP>
__attribute__((flatten, always_inline)) static inline void run_op(
  LCore& core, const LInst& I, Peripheral& p
) { library_t::Operation<OP>::template run<LSpec>(core, I, PROG, p); }

#ifdef SGPL_MARK
#define FMARK(POS, WMASK) \
  for (int r{}; r < 8; ++r) if ((WMASK) >> r & 1) \
    core.registers[r] = mark(core.registers[r], POS);
#else
#define FMARK(POS, WMASK)
#endif

// Maybe-skip: an opaque choice between running an instruction and not running
// it. If the optimizer deletes the skip() call, the choice cannot affect
// anything observable, so the instruction is dead. SKIP_ONE selects which
// position is skippable in the functions that follow (-2: every position).
extern "C" { __attribute__((pure)) bool skip(int pos) noexcept; }
#define SKIP_ONE (-1)
#ifdef SGPL_SKIP
#define SKIP_PRED(POS) (SKIP_ONE == -2 || (POS) == SKIP_ONE)
#ifdef SGPL_GATED
// Two-stage use: compile once with every skip behind a switch the optimizer
// cannot see, save the IR, then set the switches per variant and re-optimize.
extern "C" { extern const unsigned char sgpl_enabled[4096]; }
// switch values: 0 = always run, 1 = maybe skip, 2 = always skip (nopped)
#define MAYBE_SKIP(POS) \
  (sgpl_enabled[POS] == 2 || (sgpl_enabled[POS] == 1 && skip(POS)))
#else
#define MAYBE_SKIP(POS) (SKIP_PRED(POS) && skip(POS))
#endif
#else
#define MAYBE_SKIP(POS) false
#endif

// Local anchors have no code to skip; what they do is exist as jump and
// regulator targets. So their switch gates that instead: an anchor that may be
// skipped may be missing from its module's local jump table. The generator
// passes each instruction's match as a chain, best match first, and the match
// is the first anchor in the chain that is present.
#define LON(POS) (!(MAYBE_SKIP(POS)))
#define LPICK(POS, REST) (LON(POS) ? (POS) : (REST))
// an anchor's contribution to its table's identity token
#define LTOK(POS, HASH) (LON(POS) ? (HASH) : 0ULL)

// One step = one small function (a lambda). The compiler reduces it against
// its constant instruction while it is still tiny, and only then inlines the
// reduced body into the module function. Only the fields a step can change
// are reset. (-DSGPL_FLAT_STEPS restores the old expand-in-place form.)
#ifdef SGPL_FLAT_STEPS
#define STEP_FN_BEGIN
#define STEP_FN_END
#else
#define STEP_FN_BEGIN [&]() __attribute__((flatten)) {
#define STEP_FN_END }();
#endif

#define FCALL_(SKIPPED, POS, OP, A, B, C, TAG, HJ, HLR, HG0, HG1, WMASK) \
  tick(POS); \
  core.pc = POS; \
  core.terminated = core.loaded = core.global_jumped = false; \
  STEP_FN_BEGIN \
    if (!(SKIPPED)) { \
      LInst I; \
      I.op_code = OP; I.args = {A, B, C}; I.tag.SetUInt64(0, TAG); \
      core.h_jump = HJ; \
      core.local.pos = POS; core.local.h_raw = HLR; \
      core.global[0].pos = POS; core.global[0].h_raw = HG0; \
      core.global[1].pos = POS; core.global[1].h_raw = HG1; \
      run_op<OP>(core, I, p); \
    } \
  STEP_FN_END \
  FMARK(POS, WMASK)
#define FCALL(POS, ...) FCALL_(MAYBE_SKIP(POS), POS, __VA_ARGS__)
#define FCALL_ALWAYS(POS, ...) FCALL_(false, POS, __VA_ARGS__) // anchors

// launched core: run the module's own global anchor, then its body
#define F_LAUNCH(NAME, BODY, ...) \
  static void NAME(FPARAMS) { \
    FCORE FCALL_ALWAYS(__VA_ARGS__) return BODY(FREGS, p); \
  }

#define F_BEGIN(NAME, HAS, TOKEN) \
  static void NAME(FPARAMS) { FCORE \
    core.local.has_anchors = HAS; \
    core.local.state = core.local.fresh = TOKEN;

#define F_STEP(J, POS, ...) \
  L##J: FCALL(POS, __VA_ARGS__) \
  if (core.terminated) return; \
  if (core.global_jumped) return f_gdispatch( \
    gmatch(core.jump_tag, core.jump_jt), FREGS, p \
  ); \
  if (core.pc != POS) goto dispatch;

// next module's global anchor: terminate, or continue into that module
#define F_END(J, NEXT, POS, ...) \
  L##J: FCALL_ALWAYS(POS, __VA_ARGS__) \
  if (core.terminated) return; \
  return NEXT(FREGS, p);
