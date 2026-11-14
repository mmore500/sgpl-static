#pragma once
// Whole-program form: every module is a block of real functor calls inside
// one function, connected by gotos (fall-through, global jump dispatch).
// Registers are locals of that one function, so the optimizer itself works
// out which values survive from one module into the next.

#include "lower_core.hpp"

extern "C" {
// regulated global match: returns a global anchor position, or -1
__attribute__((pure)) int gmatch(uint64_t tag, int jt) noexcept;
}

// Diagnostic build (-DSGPL_MARK): tag every register an instruction writes
// with an opaque marker carrying the instruction's position. A marker that
// survives optimization means that write is observably used. Used only to
// propose candidates; markers block constant folding, so verdicts come from
// comparing clean builds.
extern "C" { __attribute__((const)) float mark(float v, int pos) noexcept; }
// In the diagnostic build, record which local anchors a jump can land on.
extern "C" { void arrived(int pos) noexcept; }
#ifdef SGPL_MARK
#define ARRIVE(POS) arrived(POS);
#else
#define ARRIVE(POS)
#endif

// WMASK: registers the generator observed this instruction writing
#ifdef SGPL_MARK
#define WCALL(POS, OP, A, B, C, TAG, HJ, HLR, HG0, HG1, WMASK) \
  CALL(POS, OP, A, B, C, TAG, HJ, HLR, HG0, HG1) \
  for (int r{}; r < 8; ++r) if ((WMASK) >> r & 1) \
    core.registers[r] = mark(core.registers[r], POS);
#else
#define WCALL(POS, OP, A, B, C, TAG, HJ, HLR, HG0, HG1, WMASK) \
  CALL(POS, OP, A, B, C, TAG, HJ, HLR, HG0, HG1)
#endif

#define PROGRAM_BEGIN(NAME) \
  __attribute__((flatten)) void NAME(Peripheral& p, const int entry) { \
    LCore core; LInst I; int gm = -1; \
    for (int r{}; r < 8; ++r) core.registers[r] = init_reg(r); \
    core.global[0].jt = 0; core.global[1].jt = 1;

// launched core: executes the module's own global anchor, then its body
#define W_ENTRY(ID, ...) E##ID: WCALL(__VA_ARGS__)

// body start: reached by launch, fall-through, or (jump mode) global jump
#define W_BODY(ID, HAS, TOKEN) \
  M##ID: core.local.has_anchors = HAS; \
  core.local.state = core.local.fresh = TOKEN;

#define W_STEP(ID, J, POS, ...) \
  L##ID##_##J: WCALL(POS, __VA_ARGS__) \
  if (core.terminated) return; \
  if (core.global_jumped) { \
    gm = gmatch(core.jump_tag, core.jump_jt); goto gdispatch; \
  } \
  if (core.pc != POS) goto D##ID;

// next module's global anchor: terminate, or continue into that module
#define W_END(ID, J, NEXT, POS, ...) \
  L##ID##_##J: WCALL(POS, __VA_ARGS__) \
  if (core.terminated) return; \
  goto M##NEXT;
