#pragma once
#ifndef SGPL_STATIC_TESTS_RANDOM_HPP_INCLUDE
#define SGPL_STATIC_TESTS_RANDOM_HPP_INCLUDE

// The test's own source of randomness, for the inputs it feeds both programs.
// It is separate from signalgp-lite's RNG, which belongs to the program under
// test.
#include <cstdint>
#include <cstring>
#include <random>

struct Random {
  std::mt19937_64 engine;

  uint64_t operator()(const uint64_t n) { return engine() % n; }

  float value() {
    switch ((*this)(6)) {
      case 0: return 0.0f;
      case 1: return 1.0f;
      case 2: return static_cast<float>((*this)(8)) - 3.0f;
      case 3: {
        const uint32_t bits = engine();
        float f;
        std::memcpy(&f, &bits, sizeof(f));
        return f;
      }
      default: return static_cast<float>(static_cast<double>((*this)(2000001)) / 1e6 - 1.0);
    }
  }
};

#endif  // #ifndef SGPL_STATIC_TESTS_RANDOM_HPP_INCLUDE
