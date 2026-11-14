#pragma once
#ifndef SGPL_STATIC_TESTS_RANDOM_HPP_INCLUDE
#define SGPL_STATIC_TESTS_RANDOM_HPP_INCLUDE

// The test's own source of randomness, for the inputs it feeds both programs.
// It is separate from signalgp-lite's RNG, which belongs to the program under
// test.
#include <cstdint>
#include <cstring>
#include <random>

// A register or state value: mostly ordinary fractions, sometimes a special
// case (zero, one, a small integer, or raw bits such as NaN and infinity).
inline float random_value(std::mt19937_64 & random) {
  switch (random() % 6) {
    case 0: return 0.0f;
    case 1: return 1.0f;
    case 2: return static_cast<float>(random() % 8) - 3.0f;
    case 3: {
      const uint32_t bits = random();
      float f;
      std::memcpy(&f, &bits, sizeof(f));
      return f;
    }
    default: return static_cast<float>(static_cast<double>(random() % 2000001) / 1e6 - 1.0);
  }
}

#endif  // #ifndef SGPL_STATIC_TESTS_RANDOM_HPP_INCLUDE
