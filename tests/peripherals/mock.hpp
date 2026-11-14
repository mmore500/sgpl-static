#pragma once
#ifndef SGPL_STATIC_TESTS_PERIPHERALS_MOCK_HPP_INCLUDE
#define SGPL_STATIC_TESTS_PERIPHERALS_MOCK_HPP_INCLUDE

// Test peripheral: stand-ins for dishtiny's peripheral ops (same names and
// register use, mock state). Runs on signalgp-lite master.
#include <array>
#include <cstring>
#include <cstdint>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "sgpl_static/shim.hpp"

#include "sgpl/hardware/Core.hpp"
#include "sgpl/library/OpLibraryCoupler.hpp"
#include "sgpl/library/prefab/CompleteOpLibrary.hpp"
#include "sgpl/operations/operations.hpp"
#include "sgpl/program/Instruction.hpp"
#include "sgpl/program/Program.hpp"
#include "sgpl/spec/Spec.hpp"

// ---------------------------------------------------------------------------
// mock peripheral + stand-ins for dishtiny's peripheral ops
// (same names, same register read/write sets as dish2/operations/*.hpp)
// ---------------------------------------------------------------------------
struct Peripheral {
  static constexpr size_t NW = 32; // writable part of own state
  static constexpr size_t NR = 96; // whole readable own state
  static constexpr size_t NN = 96; // neighbor state
  std::array<float, NR> own{};
  std::array<float, NN> nbr{};
};

static uint32_t fbits(const float f) {
  uint32_t bits;
  std::memcpy(&bits, &f, sizeof(bits));
  return bits;
}

#define STUB_META(NAME)                                                       \
  static std::string name() { return NAME; }                                  \
  static size_t prevalence() { return 1; }                                    \
  template <typename Spec>                                                    \
  static auto descriptors(const sgpl::Instruction<Spec>&) {                   \
    return std::map<std::string, std::string>{};                              \
  }                                                                           \
  template <typename Spec>                                                    \
  static std::set<std::string> categories(const sgpl::Instruction<Spec>&) {   \
    return {"op", "extrinsic"};                                               \
  }

#define STUB_SIG                                                              \
  template <typename Spec>                                                    \
  static void run(                                                            \
    sgpl::Core<Spec>& core, const sgpl::Instruction<Spec>& inst,              \
    const sgpl::Program<Spec>&, typename Spec::peripheral_t& p                \
  ) noexcept

template <typename Inst> size_t waddr(const Inst& i) {
  return i.tag.GetUInt(0) % Peripheral::NW;
}
template <typename Inst> size_t raddr(const Inst& i) {
  return i.tag.GetUInt(0) % Peripheral::NR;
}
template <typename Inst> size_t naddr(const Inst& i) {
  return i.tag.GetUInt(0) % Peripheral::NN;
}

struct AddToOwnState {
  STUB_SIG { p.own[waddr(inst)] += core.registers[inst.args[0]]; }
  STUB_META("Add To Own State")
};
struct MultiplyOwnState {
  STUB_SIG { p.own[waddr(inst)] *= core.registers[inst.args[0]]; }
  STUB_META("Multiply Own State")
};
struct WriteOwnStateIf {
  STUB_SIG {
    if (!core.registers[inst.args[1]]) return;
    p.own[waddr(inst)] = core.registers[inst.args[0]];
  }
  STUB_META("Write Own State")
};
struct ReadOwnState {
  STUB_SIG { core.registers[inst.args[0]] = p.own[raddr(inst)]; }
  STUB_META("Read Own State")
};
struct ReadNeighborState {
  STUB_SIG { core.registers[inst.args[0]] = p.nbr[naddr(inst)]; }
  STUB_META("Read Neighbor State")
};
template <int KIND> struct SendMessageIf {
  STUB_SIG {
    if (!core.registers[inst.args[0]]) return;
    sgpl_static::observe(0xABCD0000 + KIND);
    sgpl_static::observe(std::hash<typename Spec::tag_t>{}(inst.tag));
    sgpl_static::observe(inst.args[0] + inst.args[1] + inst.args[2]);
    for (const float r : core.registers) sgpl_static::observe(r);
  }
  static std::string name() {
    return KIND == 0 ? "Send Inter-Cell Message"
      : KIND == 1 ? "Send Intra-Cell Message"
      : "Broadcast Intra-Cell Message";
  }
  static size_t prevalence() { return 1; }
  template <typename Spec>
  static auto descriptors(const sgpl::Instruction<Spec>&) {
    return std::map<std::string, std::string>{};
  }
  template <typename Spec>
  static std::set<std::string> categories(const sgpl::Instruction<Spec>&) {
    return {"op", "extrinsic"};
  }
};

using library_t = sgpl::OpLibraryCoupler<
  sgpl::CompleteOpLibrary,
  sgpl::global::RegulatorAdj<1>,
  sgpl::global::RegulatorGet<1>,
  sgpl::global::RegulatorSet<1>,
  sgpl::global::RegulatorDecay<1>,
  AddToOwnState,
  SendMessageIf<2>,
  MultiplyOwnState,
  ReadNeighborState,
  ReadOwnState,
  SendMessageIf<0>,
  SendMessageIf<1>,
  WriteOwnStateIf
>;

struct spec_t : public sgpl::Spec<library_t, Peripheral> {
  static constexpr inline size_t num_cores{32};
};

namespace sgpl_static { using spec_t = ::spec_t; }

#endif // #ifndef SGPL_STATIC_TESTS_PERIPHERALS_MOCK_HPP_INCLUDE
