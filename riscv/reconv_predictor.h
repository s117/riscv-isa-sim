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
#include <unordered_map>

#define RECONV_POINT_INVALID (INT64_MAX)
#define RECONV_POINT_RETURN  (UINT64_MAX-1)

class potential_reconv_point {
private:
  uint64_t ul_PotentialPC;
  bool b_HitReturn;
  bool b_ARTaken;
  bool b_ARNTaken;
  bool b_ReachedFirst;

public:


  void set_potential_pc(uint64_t new_pc) { ul_PotentialPC = new_pc; };

  void invalidate_potential_pc() { set_potential_pc(RECONV_POINT_INVALID); };

  uint64_t get_potential_pc() const { return ul_PotentialPC; };

  bool test_potential_pc_valid() const { return ul_PotentialPC != RECONV_POINT_INVALID; };

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
  uint64_t CntActivate;
  uint64_t CntEarlyDeactivate;
public:
  void initialize(uint64_t br_pc);

  uint8_t increase_call_level();

  void decrease_call_level(uint8_t old_active_bits);

  void activate_entry(bool br_taken);

  void train_entry(uint64_t commit_pc);

  void early_deactivate_entry();

  uint64_t get_activate_cnt() const { return CntActivate; };

  uint64_t get_early_deactivate_cnt() const { return CntEarlyDeactivate; };

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

  uint64_t predict(uint64_t pc) const;

  void activate(uint64_t pc, bool br_taken);

  void deactivate_all();

  void train(uint64_t commit_pc);

  void increase_call_level();

  void decrease_call_level();

  void reset();

  std::string dump_result();
};

class BFT {
public:
  struct br_stat {
    uint64_t total_cnt;
    uint64_t curr_major_target;
    uint64_t curr_major_cnt;

    std::map<uint64_t, uint64_t> cnt_by_br_target;

    br_stat() : total_cnt(0), curr_major_target(0), curr_major_cnt(0) {};
  };

  bool m_static_stats = false;

  const BFT::br_stat &get_stat(uint64_t pc);

  bool is_filtered(uint64_t pc);

  bool is_uncommon_target(uint64_t pc, uint64_t npc);

  void train(uint64_t pc, uint64_t npc, bool branch_taken);

  std::string dump_result();

  bool load_static_stat_from_file(const std::string &filepath);

  std::map<uint64_t, br_stat> m_branches_history;

  bool is_static_mode() const { return m_static_stats; };
};

class reconv_predictor {
public:
  virtual bool contains(uint64_t br_pc) const = 0;

  virtual uint64_t predict(uint64_t br_pc) const = 0;
};

class dynamic_reconv_predictor : public reconv_predictor {
public:
  BFT m_BFT;
  RPT m_RPT;

  void on_branch_retired(uint64_t pc, uint64_t npc, bool outcome);

  void on_indirect_jmp_retired(uint64_t pc, uint64_t npc);

  void on_other_insn_retired(uint64_t pc);

  void on_function_call(uint64_t pc, uint64_t target_addr);

  void on_function_return(uint64_t pc, uint64_t return_addr);

  std::string dump_RPT_result_csv();

  std::string dump_BFT_result_csv();

  bool contains(uint64_t br_pc) const override;

  uint64_t predict(uint64_t br_pc) const override;

};

class static_reconv_predictor : public reconv_predictor {
private:
  std::map<uint64_t, uint64_t> m_br_reconv_point;
  bool m_loaded = false;

public:
  bool contains(uint64_t br_pc) const override;

  uint64_t predict(uint64_t br_pc) const override;

  bool is_loaded() const { return m_loaded; };

  bool load_from_csv(const std::string &filepath);
};

#endif //RISCV_ISA_SIM_RECONV_PREDICTOR_H
