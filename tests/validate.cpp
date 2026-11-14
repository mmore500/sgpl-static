// Test only: runs the original and the stripped program side by side in the
// real signalgp-lite engine, on randomized inputs, and compares everything
// observable. A difference means the stripped program is wrong.
//
//   validate <original.json> <stripped.json> <trials> <updates> <seconds>
//
// Runs at least <trials> trials, then keeps fuzzing until <seconds> have
// passed. Each trial has its own seed, printed on a mismatch.
//
// Built against a peripheral header plus a harness header ("harness.hpp")
// that says how to randomize that peripheral and what to compare.

#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#include "peripheral.hpp"

#include "random.hpp"

#include "harness.hpp"

#include "sgpl/../../third-party/cereal/include/cereal/archives/json.hpp"
#include "sgpl/../../third-party/cereal/include/cereal/types/vector.hpp"
#include "sgpl/algorithm/execute_cpu.hpp"
#include "sgpl/hardware/Cpu.hpp"
#include "sgpl/program/Program.hpp"
#include "sgpl/utility/ThreadLocalRandom.hpp"

using spec_t      = sgpl_static::spec_t;
using program_t   = sgpl::Program<spec_t>;
using tag_t       = spec_t::tag_t;
using registers_t = sgpl::Core<spec_t>::registers_t;

static std::vector<uint64_t> trace;

void sgpl_static_observe(const uint64_t value) noexcept { trace.push_back(value); }

// inputs change between updates, identically for both programs
static uint64_t input_salt;

uint64_t sgpl_static_input(const uint64_t key) noexcept {
  return std::hash<uint64_t>{}(key * 0x9E37'79B9'7F4A'7C15ULL + input_salt) >> 7;
}

static program_t load_program(const std::string & path) {
  std::ifstream ifs(path);
  program_t prog;
  cereal::JSONInputArchive archive(ifs);
  archive(prog);
  return prog;
}

// `tags`: the original program's tags, so both runs launch the same cores
static std::vector<uint64_t> run(const program_t & prog,
                                 const std::vector<tag_t> & tags,
                                 const int seed,
                                 const int updates) {
  sgpl::tlrand.Reseed(seed);
  std::mt19937_64 random(seed * 7919 + 13);
  auto random_tag = [&]() {
    tag_t tag = tags[random() % tags.size()];
    if (random() % 2) {
      for (int k = random() % 6; k > 0; --k) { tag.Toggle(random() % 64); }
    }
    return tag;
  };

  sgpl::Cpu<spec_t> cpu;
  cpu.InitializeAnchors(prog);
  Harness harness;
  harness.init(prog, random);
  trace.clear();
  for (int update{}; update < updates; ++update) {
    input_salt = random();
    harness.perturb(random);
    for (int k = random() % 3; k > 0; --k) { cpu.ForceLaunchCore(random_tag()); }
    for (int k = random() % 3; k > 0; --k) {
      const auto tag          = random_tag();
      const size_t jump_table = random() % 2;
      registers_t registers;
      for (auto & reg : registers) { reg = BadRandom{}(random); }
      if (cpu.TryLaunchCore(tag, jump_table)) { cpu.GetFreshestCore().SetRegisters(registers); }
    }
    sgpl::execute_cpu<spec_t>(1 + random() % 24, cpu, prog, harness.peripheral);
    if (random() % 3 == 0) { cpu.DecayGlobalRegulators(); }
    trace.push_back(0xFEED0000 + update);
    trace.push_back(cpu.GetCyclesSinceConstruction());
    trace.push_back(cpu.GetNumBusyCores());
    for (size_t i{}; i < cpu.GetNumBusyCores(); ++i) {
      trace.push_back(cpu.GetCore(i).GetProgramCounter());
    }
    harness.snapshot();
  }
  return trace;
}

int main(const int argc, char ** argv) {
  if (argc != 6) {
    std::cerr << "usage: validate <original> <stripped> <trials> <updates> <seconds>\n";
    return 2;
  }
  const program_t original = load_program(argv[1]);
  const program_t stripped = load_program(argv[2]);
  const int min_trials = std::atoi(argv[3]), updates = std::atoi(argv[4]);
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(std::atoi(argv[5]));
  std::vector<tag_t> tags;
  for (const auto & inst : original) { tags.push_back(inst.tag); }

  size_t failures{}, events{};
  int trials{};
  for (int seed = 1; seed <= min_trials || std::chrono::steady_clock::now() < deadline; ++seed) {
    ++trials;
    const auto expected = run(original, tags, seed, updates);
    const auto actual   = run(stripped, tags, seed, updates);
    events += expected.size();
    if (expected != actual) {
      ++failures;
      std::cout << "MISMATCH seed " << seed << "\n";
    }
  }
  std::cout << "trials " << trials << " failures " << failures << " trace_events " << events
            << "\n";
  return failures ? 1 : 0;
}
