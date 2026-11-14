#pragma once
// Peripheral for dishtiny (github.com/mmore500/dishtiny).
//
// dishtiny's real ops and real state layout are used unchanged. What the
// peripheral connects to is replaced: each member below is either observable
// (ObservedOutput) or a no-op (Discarded). To declare something a no-op,
// change its type.
#include <array>
#include <cstddef>
#include <functional>
#include <tuple>

#include "sgpl_static/shim.hpp"

#ifndef DISH2_SPEC
#define DISH2_SPEC dish2::Spec_prq49
#endif
#include "include/dish2/spec/Spec.hpp"

namespace sgpl_static {

using dish_t = DISH2_SPEC;
using readable_t = dish2::ReadableState<dish_t>;

// A message output whose sends are observable.
struct ObservedOutput {
  int channel;
  template<typename Message>
  bool TryPut(const Message& message) {
    observe(channel);
    observe(std::hash<dish_t::tag_t>{}(std::get<0>(message)));
    for (const float reg : std::get<1>(message)) observe(reg);
    return true;
  }
};

// A no-op: whatever is sent here is dropped.
struct Discarded {
  template<typename Message> bool TryPut(const Message&) { return true; }
  template<typename Message> void push_back(const Message&) {}
};

struct NeighborState {
  readable_t state{};
  const readable_t& Get() const { return state; }
};

struct Peripheral {
  // own state: Read / Write / Add To / Multiply Own State
  readable_t readable_state{};
  // Read Neighbor State
  NeighborState state_node_input;
  // Send Inter-Cell Message
  ObservedOutput message_node_output{0};
  // Send / Broadcast Intra-Cell Message: one output per other cardinal
  std::array<ObservedOutput, 3> intra_message_node_outputs{{{1}, {2}, {3}}};
  // only used under dishtiny's self-send perturbation
  Discarded inter_message_selfsend_buffer;
  Discarded intra_message_selfsend_buffer;
  size_t root_id{};
};

// dishtiny's op library on the peripheral above
struct spec_t : public sgpl::Spec<dish_t::op_library_t, Peripheral> {
  static constexpr inline size_t num_cores{ dish_t::sgpl_spec_t::num_cores };
};

} // namespace sgpl_static
