// How to exercise peripherals/dishtiny.hpp in the engine.
#include <utility>

struct Harness {
  using readable_t = sgpl_static::readable_t;
  static constexpr size_t num_introspective
    = dish2::IntrospectiveState<sgpl_static::dish_t>::GetSize();

  sgpl_static::Peripheral peripheral;

  // set leaves [begin, end) of a state tree, each with probability 1/odds
  template<size_t... Leaf>
  static void randomize(
    readable_t& state, Random& random, const size_t begin, const size_t end,
    const size_t odds, std::index_sequence<Leaf...>
  ) {
    const auto set = [&](auto& leaf, const size_t index) {
      if (index < begin || index >= end || random(odds)) return;
      leaf = uitsl::clamp_cast<std::decay_t<decltype(leaf)>>(random.value());
    };
    (set(state.template GetByIndex<Leaf>(), Leaf), ...);
  }
  static void randomize(
    readable_t& state, Random& random, const size_t end, const size_t odds
  ) {
    randomize(
      state, random, 0, end, odds,
      std::make_index_sequence<readable_t::GetSize()>{}
    );
  }

  void init(const program_t&, Random& random) {
    randomize(peripheral.readable_state, random, readable_t::GetSize(), 1);
    randomize(peripheral.state_node_input.state, random, readable_t::GetSize(), 1);
  }
  // the host changes introspective state, and the neighbor's state
  void perturb(Random& random) {
    randomize(peripheral.readable_state, random, num_introspective, 4);
    randomize(peripheral.state_node_input.state, random, readable_t::GetSize(), 4);
  }
  void snapshot() const {
    for (size_t i{}; i < readable_t::GetSize(); ++i)
      sgpl_static::observe(peripheral.readable_state.Read(i));
  }
};
