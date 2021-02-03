//
// Created by s117 on 1/20/21.
// The Reconvergence predictor basing on https://cseweb.ucsd.edu/~tullsen/reconverge.pdf

#ifndef RISCV_ISA_SIM_RECONV_PREDICTOR_H
#define RISCV_ISA_SIM_RECONV_PREDICTOR_H

#include <map>
#include <stack>
#include <cstdint>
#include <cassert>
#include <string>
#include <sstream>
#include <iomanip>
#include <iostream>

class potential_reconv_point {
private:
  uint64_t ul_PotentialPC;
  bool b_HitReturn;
  bool b_ARTaken;
  bool b_ARNTaken;
  bool b_ReachedFirst;

public:
  static const uint64_t INVALID_POTENTIAL = UINT64_MAX;

  void set_potential_pc(uint64_t new_pc) { ul_PotentialPC = new_pc; };

  void invalidate_potential_pc() { set_potential_pc(INVALID_POTENTIAL); };

  uint64_t get_potential_pc() const { return ul_PotentialPC; };

  bool test_potential_pc_valid() const { return ul_PotentialPC != INVALID_POTENTIAL; };

  void set_HitReturn() { b_HitReturn = true; };

  void clear_HitReturn() { b_HitReturn = false; };

  bool test_HitReturn() const { return b_HitReturn; };

  void set_ARTaken() { b_ARTaken = true; };

  void clear_ARTaken() { b_ARTaken = false; };

  bool test_ARTaken() const { return b_ARTaken; };

  void set_ARNTaken() { b_ARNTaken = true; };

  void clear_ARNTaken() { b_ARNTaken = false; };

  bool test_ARNTaken() const { return b_ARNTaken; };

  void set_ReachedFirst() { b_ReachedFirst = true; };

  void clear_ReachedFirst() { b_ReachedFirst = false; };

  bool test_ReachedFirst() const { return b_ReachedFirst; };

};

class RPT_entry {
private:
  uint64_t BranchPC;
  bool LastBranchTaken;
  bool BelowPotentialActive;
  bool AbovePotentialActive;
  bool ReboundPotentialActive;
  bool BelowPotentialReached;
  bool AbovePotentialReached;
  bool ReboundPotentialReached;
  potential_reconv_point BelowPotential;
  potential_reconv_point AbovePotential;
  potential_reconv_point ReboundPotential;

public:
  void initialize(uint64_t br_pc);

  uint8_t increase_call_level();

  void decrease_call_level(uint8_t old_active_bits);

  void activate_entry(bool br_taken);

  void train_entry(uint64_t commit_pc);

  void early_deactivate_entry();

  struct prediction_details {
    int reason_id;
    int cat_id;
  };

  uint64_t make_prediction(prediction_details *prediction_details = nullptr) const;

private:
  uint8_t dump_status_bits() const;

  void restore_status_bits(uint8_t bits);

  static uint64_t pc_next(uint64_t pc) { return pc + 4; };

  void initialize_BelowPotential();

  void initialize_AbovePotential();

  void initialize_ReboundPotential();

  void set_all_ReachFirst();

  void reach_BelowPotential();

  void reach_AbovePotential();

  void reach_ReboundPotential();

  void update_BelowPotential(uint64_t new_potential_pc);

  void update_AbovePotential(uint64_t new_potential_pc);

  void update_ReboundPotential(uint64_t new_potential_pc);

};

// The Reconvergence Prediction Table indexed by the PC of branches
class RPT {
public:
  static const size_t LIMIT_MAX_CALL_DEPTH = 256;
  static const size_t INIT_CALL_DEPTH = LIMIT_MAX_CALL_DEPTH / 2;
  size_t m_current_call_depth = INIT_CALL_DEPTH;
  std::map<uint64_t, uint8_t> m_active_record[LIMIT_MAX_CALL_DEPTH];
  std::map<uint64_t, RPT_entry> m_RPT_entries;

public:
  RPT();

  bool contains(uint64_t pc) const;

  void activate(uint64_t pc, bool br_taken);

  void deactivate_all();

  void train(uint64_t commit_pc);

  void increase_call_level();

  void decrease_call_level();

  void reset();
};

class BFT {
public:
  struct br_stat {
    uint64_t cnt_taken;
    uint64_t cnt_ntaken;

    br_stat() : cnt_taken(0), cnt_ntaken(0) {};
  };

  br_stat get_stat(uint64_t pc);

  bool filter(uint64_t pc);

  void train(uint64_t pc, bool branch_taken);

  std::map<uint64_t, br_stat> m_branches_history;
};

class reconv_predictor {
private:
  BFT m_BFT;
  RPT m_RPT;
public:
  void on_branch_retired(uint64_t pc, bool outcome);

  void on_indirect_jmp_retired(uint64_t pc);

  void on_other_insn_retired(uint64_t pc);

  void on_function_call(uint64_t pc, uint64_t target_addr);

  void on_function_return(uint64_t pc, uint64_t return_addr);

  std::string dump_RPT_result_csv();

  std::string dump_BFT_result_csv();

};


#endif //RISCV_ISA_SIM_RECONV_PREDICTOR_H
