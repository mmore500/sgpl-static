#pragma once
#ifndef SGPL_STATIC_TESTS_RANDOM_HPP_INCLUDE
#define SGPL_STATIC_TESTS_RANDOM_HPP_INCLUDE

// The test's own source of randomness, for the inputs it feeds both programs.
// It is separate from signalgp-lite's RNG, which belongs to the program under
// test.
#include <cstdint>
#include <cstring>
#include <random>

// Stress-test values for registers and state, used like a standard
// distribution: BadRandom{}(engine). A third of the draws are ordinary
// fractions; the rest are awkward cases.
struct BadRandom {
  float operator()(std::mt19937_64 & random) const {
    switch (random() % 6) {
      case 0: return 0.0f;
      case 1: return 1.0f;
      case 2: return static_cast<float>(random() % 8) - 3.0f;  // small integer
      case 3: {  // raw bits: NaN, infinity, huge and tiny values
        const uint32_t bits = random();
        float f;
        std::memcpy(&f, &bits, sizeof(f));
        return f;
      }
      default: return std::uniform_real_distribution<float>{-1.0f, 1.0f}(random);
    }
  }
};

#endif  // #ifndef SGPL_STATIC_TESTS_RANDOM_HPP_INCLUDE
