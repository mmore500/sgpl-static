#pragma once
// Peripheral for Symbulation's SGP mode (github.com/anyaevostinar/SymbulationEmp).
//
// Symbulation's real instructions are used unchanged. They talk to a
// CPUState<World>; this header supplies that for a stand-in World. Each thing
// an instruction can do to it is either observable (observe) or a no-op
// (an empty body).
#include <cstddef>
#include <cstdint>
#include <vector>

#include "sgpl_static/shim.hpp"

#include "source/sgp_mode/hardware/GenomeLibrary.h"
#include "source/sgp_mode/hardware/SGPHardwareSpec.h"

#include "sgpl/hardware/JumpTable.hpp"

namespace sgpl_static {

enum Event : uint64_t { REPRODUCE = 1, OUTPUT, DONATE, STEAL, INFECT };

// What the SenseTask instruction asks about tasks: unknown inputs.
struct TaskRequirement { double task_value{}; };
struct TaskIO {
  emp::vector<size_t> task_ids{0};
  bool IsValidOutput(const uint32_t output) const { return input(output) & 1; }
  const emp::vector<size_t>& GetTaskIDs(uint32_t) const { return task_ids; }
};
struct IOBank {
  TaskIO io;
  const TaskIO& GetIO(size_t) const { return io; }
};
struct TaskEnvironment {
  IOBank bank;
  TaskRequirement requirement;
  IOBank& GetIOBank() { return bank; }
  bool IsHostTask(const size_t id) const { return input(id + 1000) & 1; }
  TaskRequirement& GetHostTaskReq(size_t) { return requirement; }
};
struct Config {
  size_t min_cycles_before_repro{};
  bool only_first_task_credit{};
  size_t HOST_MIN_CYCLES_BEFORE_REPRO() const { return min_cycles_before_repro; }
  size_t SYM_MIN_CYCLES_BEFORE_REPRO() const { return min_cycles_before_repro; }
  bool HOST_ONLY_FIRST_TASK_CREDIT() const { return only_first_task_credit; }
};

struct World {
  Config config;
  TaskEnvironment task_environment;
  const Config& GetConfig() const { return config; }
  TaskEnvironment& GetTaskEnv() { return task_environment; }
  template<typename State, typename Requirement>
  bool CanPerformTask(const State&, const Requirement&) const {
    return input(2000) & 1;
  }
  // interactions between organisms: observable
  template<typename... Org> void SymDonateToHost(Org&&...) { observe(DONATE); }
  template<typename... Org> void SymStealFromHost(Org&&...) { observe(STEAL); }
  template<typename... Org> void FreeLivingSymDoInfect(Org&&...) { observe(INFECT); }
};

// IO instruction: every output is observable
struct OutputBuffer {
  void emplace_back(const uint32_t value) { observe(OUTPUT); observe(value); }
};

} // namespace sgpl_static

template<>
class sgpmode::CPUState<sgpl_static::World> {
public:
  sgpl_static::World world;
  Stacks<uint32_t> stacks{2};
  RingBuffer<uint32_t> input_buffer{4, 0};
  sgpl_static::OutputBuffer output_buffer;
  emp::BitVector first_task_performed{1};
  emp::WorldPosition location{0};
  std::vector<size_t> jump_table; // filled by whoever runs the engine
  size_t cpu_cycles_since_repro{}, task_env_id{};
  bool is_host{true}, has_host{}, repro_in_progress{}, repro_attempt{};
  int organism{}, host{};

  sgpl_static::World& GetWorld() { return world; }
  Stacks<uint32_t>& GetStacks() { return stacks; }
  RingBuffer<uint32_t>& GetInputBuffer() { return input_buffer; }
  sgpl_static::OutputBuffer& GetOutputBuffer() { return output_buffer; }
  emp::BitVector& GetFirstTaskPerformed() { return first_task_performed; }
  emp::WorldPosition GetLocation() const { return location; }
  size_t GetCPUCyclesSinceRepro() const { return cpu_cycles_since_repro; }
  size_t GetTaskEnvID() const { return task_env_id; }
  bool IsHost() const { return is_host; }
  bool HasHost() const { return has_host; }
  bool ReproInProgress() const { return repro_in_progress; }
  bool ReproAttempt() const { return repro_attempt; }
  int& GetOrg() { return organism; }
  int& GetHost() { return host; }
  bool OutputCredited(const size_t task, const uint32_t output) const {
    return sgpl_static::input(3000 + task + output) & 1;
  }
  void MarkReproAttempt() {
    repro_attempt = true;
    sgpl_static::observe(sgpl_static::REPRODUCE);
  }
  // Where a jump instruction at `pc` goes. Symbulation fixes this per
  // program, so the analysis is given it as a constant (see jump_dests).
  size_t GetJumpDest(const size_t pc) const {
#ifdef SGPL_STATIC_PROGRAM_SIZE
    return SGPL_STATIC_JUMP_DEST[pc];
#else
    return pc < jump_table.size() ? jump_table[pc] : pc + 1;
#endif
  }
};

namespace sgpl_static {

using spec_t = sgpmode::SGPHardwareSpec<
  sgpmode::Library, sgpmode::CPUState<World>, World
>;

// Per position: where the jump instruction there sets the program counter,
// or -1 if it is not a jump. Mirrors SGPHardware::InitializeLocalJumpTable.
#define SGPL_STATIC_JUMP_DESTS
inline std::vector<int> jump_dests(const sgpl::Program<spec_t>& program) {
  sgpl::JumpTable<spec_t, spec_t::global_matching_t> table;
  table.InitializeGlobalAnchors(program);
  std::vector<int> dests(program.size(), -1);
  for (size_t pos{}; pos < program.size(); ++pos) {
    const std::string name = program[pos].GetOpName();
    if (name != "JumpIfNEq" && name != "JumpIfLess" && name != "JumpIfEq")
      continue;
    const auto match = table.MatchRegulated(program[pos].tag);
    dests[pos] = match.size() ? table.GetVal(match.front()) : pos + 1;
  }
  return dests;
}

} // namespace sgpl_static
