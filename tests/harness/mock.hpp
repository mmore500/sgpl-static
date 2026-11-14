// How to exercise tests/peripherals/mock.hpp in the engine.
struct Harness {
  Peripheral peripheral;
  void init(const program_t&, Random& random) {
    for (auto& v : peripheral.own) v = random.value();
    for (auto& v : peripheral.nbr) v = random.value();
  }
  // the host changes the read-only part of own state, and the neighbor's
  void perturb(Random& random) {
    for (size_t i = Peripheral::NW; i < Peripheral::NR; ++i)
      if (random(4) == 0) peripheral.own[i] = random.value();
    for (auto& v : peripheral.nbr) if (random(4) == 0) v = random.value();
  }
  void snapshot() const {
    for (const float v : peripheral.own) sgpl_static::observe(v);
  }
};
