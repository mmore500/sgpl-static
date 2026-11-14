#pragma once
// Analysis-only stand-in for sgpl::Core, used by generated per-module programs.
//
// Generated code calls the library's real op functors, unmodified. Only the
// Core they operate on is swapped (by specializing sgpl::Core for LSpec):
// registers are real, but matching, forking and regulator state are opaque
// externs, so the optimizer sees exactly what each op reads and writes.

#include "mock_spec.hpp"

struct LSpec : public spec_t {};

extern "C" {
void tick(int pos) noexcept;                       // one instruction of time
__attribute__((pure)) float init_reg(int r) noexcept;
// regulated local match: returns a local anchor position, or -1
__attribute__((pure)) int lmatch(uint64_t table, uint64_t tag) noexcept;
__attribute__((const)) uint64_t lreg_mod(
  uint64_t table, int kind, int pos, uint32_t amt
) noexcept;
__attribute__((pure)) float lreg_view(uint64_t table, int anchor) noexcept;
__attribute__((pure)) float greg_view(int jt, int anchor) noexcept;
void greg_mod(int jt, int kind, int pos, uint32_t amt) noexcept;
void fork_request(uint64_t tag) noexcept;
void exit_global_jump(
  uint64_t tag, int jt,
  float, float, float, float, float, float, float, float
) noexcept;
void exit_fallthrough(
  int pos, float, float, float, float, float, float, float, float
) noexcept;
}

struct LMatches {
  size_t uid;
  size_t n;
  const size_t* begin() const { return &uid; }
  const size_t* end() const { return &uid + n; }
  size_t size() const { return n; }
  size_t front() const { return uid; }
};

static inline uint32_t lbits(const float v) {
  return std::bit_cast<uint32_t>(v);
}

// local jump table: contents summarized by an opaque state token
struct LLocalTable {
  using tag_t = LSpec::tag_t;
  bool has_anchors{};
  uint64_t state{}, fresh{};
  int pos{}, h_raw{-1};
  LMatches MatchRaw(const tag_t&) const {
    return {(size_t)h_raw, (has_anchors && h_raw >= 0) ? 1u : 0u};
  }
  void SetRegulator(size_t, float v) { state = lreg_mod(state, 0, pos, lbits(v)); }
  void AdjRegulator(size_t, float v) { state = lreg_mod(state, 1, pos, lbits(v)); }
  void DecayRegulator(size_t, int32_t v) { state = lreg_mod(state, 2, pos, v); }
  float ViewRegulator(size_t uid) const { return lreg_view(state, uid); }
  size_t GetSize() const { return has_anchors; }
};

// global jump tables: shared between cores, so writes are observable
struct LGlobalTable {
  using tag_t = LSpec::tag_t;
  int jt{}, pos{}, h_raw{-1};
  LMatches MatchRaw(const tag_t&) const {
    return {(size_t)h_raw, h_raw >= 0 ? 1u : 0u};
  }
  void SetRegulator(size_t, float v) { greg_mod(jt, 0, pos, lbits(v)); }
  void AdjRegulator(size_t, float v) { greg_mod(jt, 1, pos, lbits(v)); }
  void DecayRegulator(size_t, int32_t v) { greg_mod(jt, 2, pos, v); }
  float ViewRegulator(size_t uid) const { return greg_view(jt, uid); }
};

template<>
class sgpl::Core<LSpec> {
public:
  using tag_t = LSpec::tag_t;
  using registers_t = std::array<float, LSpec::num_registers>;
  registers_t registers{};
  LLocalTable local;
  std::array<LGlobalTable, 2> global;
  size_t pc{};
  int h_jump{-1};       // unregulated local match for this instruction's tag
  bool terminated{}, loaded{}, global_jumped{};
  uint64_t jump_tag{};
  int jump_jt{};

  void Terminate() noexcept { terminated = true; }
  bool HasTerminated() const noexcept { return terminated; }
  size_t GetProgramCounter() const noexcept { return pc; }
  bool HasLocalAnchors() const noexcept { return local.has_anchors; }
  void LoadLocalAnchors(const sgpl::Program<LSpec>&) noexcept { loaded = true; }
  void JumpToGlobalAnchorMatch(const tag_t& q, const size_t jt=0) noexcept {
    global_jumped = true;
    jump_tag = q.GetUInt64(0);
    jump_jt = jt;
  }
  void JumpToLocalAnchorMatch(const tag_t& q) noexcept {
    if (!local.has_anchors) return;
    // untouched regulators: the match is the precomputed one
    const int dest = (local.state == local.fresh)
      ? h_jump : lmatch(local.state, q.GetUInt64(0));
    if (dest >= 0) pc = dest;
  }
  auto& GetLocalJumpTable() noexcept { return local; }
  auto& GetGlobalJumpTable(const size_t jt=0) noexcept { return global[jt]; }
  bool RequestFork(const tag_t& tag) noexcept {
    fork_request(tag.GetUInt64(0));
    return true;
  }
  void ResetRegisters() noexcept { registers.fill({}); }
  void SetRegisters(const registers_t& set) noexcept { registers = set; }
  const registers_t& GetRegisters() noexcept { return registers; }
};

using LCore = sgpl::Core<LSpec>;
using LInst = sgpl::Instruction<LSpec>;
extern const sgpl::Program<LSpec> PROG; // never read by the analysis core

// only registers that some destination can observably read leave a module
#define XR(MASK, K) ((MASK) >> K & 1 ? core.registers[K] : 0.0f)
#define REGS8(MASK) \
  XR(MASK, 0), XR(MASK, 1), XR(MASK, 2), XR(MASK, 3), \
  XR(MASK, 4), XR(MASK, 5), XR(MASK, 6), XR(MASK, 7)

// FALL_LIVE / JUMP_LIVE: registers live after falling through into the next
// module / after a global jump (bit k = register k)
#define MODULE_BEGIN(NAME, FALL_LIVE, JUMP_LIVE) \
  __attribute__((flatten)) void NAME(Peripheral& p) { \
    const unsigned fall_live = FALL_LIVE, jump_live = JUMP_LIVE; \
    LCore core; LInst I; \
    for (int r{}; r < 8; ++r) core.registers[r] = init_reg(r); \
    core.global[0].jt = 0; core.global[1].jt = 1;

// call the real functor for one instruction
#define CALL(POS, OP, A, B, C, TAG, HJ, HLR, HG0, HG1) \
  tick(POS); \
  I.op_code = OP; I.args = {A, B, C}; I.tag.SetUInt64(0, TAG); \
  core.pc = POS; core.h_jump = HJ; \
  core.local.pos = POS; core.local.h_raw = HLR; \
  core.global[0].pos = POS; core.global[0].h_raw = HG0; \
  core.global[1].pos = POS; core.global[1].h_raw = HG1; \
  core.terminated = core.loaded = core.global_jumped = false; \
  library_t::Operation<OP>::template run<LSpec>(core, I, PROG, p);

// module's own global anchor: loads the module's local anchors
#define STEP_ENTRY(J, HAS, TOKEN, ...) \
  L##J: CALL(__VA_ARGS__) \
  core.local.has_anchors = HAS; core.local.state = core.local.fresh = TOKEN;

#define STEP(J, POS, ...) \
  L##J: CALL(POS, __VA_ARGS__) \
  if (core.terminated) return; \
  if (core.global_jumped) { \
    exit_global_jump(core.jump_tag, core.jump_jt, REGS8(jump_live)); return; \
  } \
  if (core.pc != POS) goto dispatch;

// next module's global anchor: terminate, or run on into the next module
#define STEP_END(J, POS, ...) \
  L##J: CALL(POS, __VA_ARGS__) \
  if (core.terminated) return; \
  exit_fallthrough(POS, REGS8(fall_live)); return;
