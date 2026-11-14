#pragma once
#ifndef SGPL_STATIC_LOWER_CORE_HPP_INCLUDE
#define SGPL_STATIC_LOWER_CORE_HPP_INCLUDE

// Analysis-only stand-in for sgpl::Core, used by generated programs.
//
// Generated code calls the library's real op functors, unmodified. Only the
// Core they operate on is swapped (by specializing sgpl::Core for LSpec):
// registers are real, but matching, forking and regulator state are opaque
// externs, so the optimizer sees exactly what each op reads and writes.

#include "peripheral.hpp" // the user's peripheral header: sgpl_static::spec_t

struct LSpec : public sgpl_static::spec_t {};
using LPeripheral = LSpec::peripheral_t;

static_assert(LSpec::num_registers == 8, "generated code passes 8 registers");
static_assert(LSpec::num_global_jump_tables == 2, "two global jump tables");

extern "C" {
void tick(int pos) noexcept;                       // one instruction of time
__attribute__((pure)) float init_reg(int r) noexcept;
// regulated global match: returns a global anchor position, or -1
__attribute__((pure)) int gmatch(uint64_t tag, int jt) noexcept;
// regulated local match: returns a local anchor position, or -1
__attribute__((pure)) int lmatch(uint64_t table, uint64_t tag) noexcept;
__attribute__((const)) uint64_t lreg_mod(
  uint64_t table, int kind, int pos, uint32_t amt
) noexcept;
__attribute__((pure)) float lreg_view(uint64_t table, int anchor) noexcept;
__attribute__((pure)) float greg_view(int jt, int anchor) noexcept;
void greg_mod(int jt, int kind, int pos, uint32_t amt) noexcept;
void fork_request(uint64_t tag) noexcept;
// an indexed jump to a position the peripheral header did not announce
__attribute__((noreturn)) void sgpl_static_bad_jump(int pos) noexcept;
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
  uint32_t bits;
  std::memcpy(&bits, &v, sizeof(bits));
  return bits;
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
  bool terminated{}, loaded{}, global_jumped{}, index_jumped{};
  uint64_t jump_tag{};
  int jump_jt{};
  size_t jump_index{};

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
#ifdef SGPL_STATIC_FLAT
  // An op sets the program counter itself (not sgpl's own jump ops). Only
  // modelled without local anchors: the real core would keep its local jump
  // table across such a jump.
  void JumpToIndex(const size_t idx) noexcept {
    index_jumped = true;
    jump_index = idx;
  }
#endif
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

#endif // #ifndef SGPL_STATIC_LOWER_CORE_HPP_INCLUDE
