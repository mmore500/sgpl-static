#pragma once
#ifndef SGPL_STATIC_LOWER_FN_HPP_INCLUDE
#define SGPL_STATIC_LOWER_FN_HPP_INCLUDE

// Macros for generated programs.
//
// Module-per-function form: each module is a function whose parameters are
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
#if __has_include("sgpl/../../third-party/conduit/include/uit_emp/math/Random.hpp")
#include "sgpl/../../third-party/conduit/include/uit_emp/math/Random.hpp"
namespace sgpl_static { using random_t = uit_emp::Random; }
#else // older signalgp-lite: Empirical directly
#include "sgpl/../../third-party/Empirical/include/emp/math/Random.hpp"
namespace sgpl_static { using random_t = emp::Random; }
#endif
extern "C" {
__attribute__((pure)) uint64_t rand_state(int site, int word) noexcept;
}
namespace sgpl {
struct AnalysisRng {
  sgpl_static::random_t rand{1};
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
  sgpl_static::random_t& Get() { return rand; }
  std::byte GetByte() { return std::byte(rand.GetUInt(256)); }
};
} // namespace sgpl
#define tlrand AnalysisRng{__COUNTER__}
// ---------------------------------------------------------------------------

#include "lower_core.hpp"

#define FPARAMS \
  float r0, float r1, float r2, float r3, float r4, float r5, float r6, \
  float r7, LPeripheral& p
#define FREGS \
  core.registers[0], core.registers[1], core.registers[2], core.registers[3], \
  core.registers[4], core.registers[5], core.registers[6], core.registers[7]
#define FCORE \
  LCore core; \
  core.registers = {r0, r1, r2, r3, r4, r5, r6, r7}; \
  core.global[0].jt = 0; core.global[1].jt = 1;

#define F_DECL(NAME) static void NAME(FPARAMS);

// force the real op body inline, but leave module-to-module calls to clang
template<size_t OP>
__attribute__((flatten, always_inline)) static inline void run_op(
  LCore& core, const LInst& I, LPeripheral& p
) {
  LSpec::library_t::template Operation<OP>::template run<LSpec>(
    core, I, PROG, p
  );
}

// Maybe-skip: an opaque choice between running an instruction and not running
// it. If the optimizer deletes the skip() call, the choice cannot affect
// anything observable, so the instruction is dead.
//
// Two-stage use: compile once with every skip behind a switch the optimizer
// cannot see, save the IR, then set the switches per variant and re-optimize.
// switch values: 0 = always run, 1 = maybe skip, 2 = always skip (nopped)
extern "C" {
__attribute__((pure)) bool skip(int pos) noexcept;
extern const unsigned char sgpl_enabled[SGPL_STATIC_PROGRAM_SIZE];
}
#define MAYBE_SKIP(POS) \
  (sgpl_enabled[POS] == 2 || (sgpl_enabled[POS] == 1 && skip(POS)))

// Local anchors have no code to skip; what they do is exist as jump and
// regulator targets. So their switch gates that instead: an anchor that may be
// skipped may be missing from its module's local jump table. The generator
// passes each instruction's match as a chain, best match first, and the match
// is the first anchor in the chain that is present.
#define LON(POS) (!(MAYBE_SKIP(POS)))
#define LPICK(POS, REST) (LON(POS) ? (POS) : (REST))
// an anchor's contribution to its table's identity token
#define LTOK(POS, HASH) (LON(POS) ? (HASH) : 0ULL)

#define FCALL_(SKIPPED, POS, OP, A, B, C, TAG, HJ, HLR, HG0, HG1) \
  tick(POS); \
  core.pc = POS; \
  core.terminated = core.loaded = false; \
  core.global_jumped = core.index_jumped = false; \
    if (!(SKIPPED)) { \
      LInst I; \
      I.op_code = OP; I.args = {A, B, C}; I.tag.SetUInt64(0, TAG); \
      core.h_jump = HJ; \
      core.local.pos = POS; core.local.h_raw = HLR; \
      core.global[0].pos = POS; core.global[0].h_raw = HG0; \
      core.global[1].pos = POS; core.global[1].h_raw = HG1; \
      run_op<OP>(core, I, p); \
    }
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

// Flat form, for programs without local anchors: every instruction is a label
// in one function, and execution runs on past global anchors.
#define FLAT_STEP(CALL, POS, ...) \
  L##POS: CALL(POS, __VA_ARGS__) \
  if (core.terminated) return; \
  if (core.global_jumped) { \
    target = gmatch(core.jump_tag, core.jump_jt); goto global_jump; \
  } \
  if (core.index_jumped) { target = core.jump_index; goto index_jump; }

#endif // #ifndef SGPL_STATIC_LOWER_FN_HPP_INCLUDE
