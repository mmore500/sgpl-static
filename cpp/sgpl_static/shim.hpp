#pragma once
// Helpers for peripheral headers.
//
// A peripheral header stands in for whatever a program's ops talk to. Each
// thing an op can do to it is one of two kinds:
//
//   observable: call sgpl_static::observe(...) with the values that matter.
//               The analysis cannot see into observe(), so every instruction
//               feeding the call is kept.
//   no-op:      do nothing. The analysis sees the empty body, so instructions
//               that only feed it are removed.
//
// Reads of the peripheral's own members are treated as inputs that may change
// between any two instructions. For an input that is not a member, use
// sgpl_static::input(key): a value the analysis knows nothing about.
#include <cstdint>
#include <cstring>
#include <type_traits>

extern "C" {
void sgpl_static_observe(uint64_t value) noexcept;
__attribute__((pure)) uint64_t sgpl_static_input(uint64_t key) noexcept;
}

namespace sgpl_static {

template<typename T>
inline void observe(const T& value) {
  static_assert(std::is_arithmetic<T>::value || std::is_enum<T>::value);
  static_assert(sizeof(T) <= sizeof(uint64_t));
  uint64_t bits{};
  std::memcpy(&bits, &value, sizeof(T));
  sgpl_static_observe(bits);
}

inline uint64_t input(const uint64_t key) { return sgpl_static_input(key); }

} // namespace sgpl_static
