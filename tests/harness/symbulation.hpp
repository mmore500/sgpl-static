#pragma once
#ifndef SGPL_STATIC_TESTS_HARNESS_SYMBULATION_HPP_INCLUDE
#define SGPL_STATIC_TESTS_HARNESS_SYMBULATION_HPP_INCLUDE

// How to exercise peripherals/symbulation/main-as-of-2026-11-14.hpp in the engine.
struct Harness {
  sgpl_static::spec_t::peripheral_t peripheral;

  void init(const sgpl::Program<sgpl_static::spec_t> & program, std::mt19937_64 & random) {
    for (const int dest : sgpl_static::jump_dests(program)) {
      peripheral.jump_table.push_back(dest);
    }
    perturb(random);
  }

  // the world changes the organism's inputs and circumstances
  void perturb(std::mt19937_64 & random) {
    for (int i{}; i < 4; ++i) { peripheral.input_buffer.push(random() % 5); }
    peripheral.cpu_cycles_since_repro               = random() % 4;
    peripheral.world.config.min_cycles_before_repro = random() % 4;
    peripheral.world.config.only_first_task_credit  = random() % 2;
    peripheral.world.task_environment.requirement.task_value =
      static_cast<double>(random() % 3) - 1;
    peripheral.first_task_performed.Set(0, random() % 2);
    peripheral.is_host           = random() % 2;
    peripheral.has_host          = random() % 2;
    peripheral.repro_in_progress = random() % 4 == 0;
    peripheral.repro_attempt     = false;
  }

  void snapshot() const {}
};

#endif  // #ifndef SGPL_STATIC_TESTS_HARNESS_SYMBULATION_HPP_INCLUDE
