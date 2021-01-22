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
  void initialize(uint64_t br_pc) {
    BranchPC = br_pc;
    LastBranchTaken = false;
    BelowPotentialActive = false;
    AbovePotentialActive = false;
    ReboundPotentialActive = false;
    BelowPotentialReached = false;
    AbovePotentialReached = false;
    ReboundPotentialReached = false;
    initialize_BelowPotential();
    initialize_AbovePotential();
    initialize_ReboundPotential();
  };

  uint8_t increase_call_level() {
    // Dump the active bits of current call level for future recovery
    uint8_t curr_status = dump_status_bits();

    // Clear all the active bit to avoid inadvertently updating the AR bit
    BelowPotentialActive = false;
    AbovePotentialActive = false;
    ReboundPotentialActive = false;
    BelowPotentialReached = false;
    AbovePotentialReached = false;
    ReboundPotentialReached = false;

    return curr_status;
  };

  void decrease_call_level(uint8_t old_active_bits) {
    // Update the HitReturn bit
    if (BelowPotentialActive) {
      BelowPotential.set_HitReturn();
    }
    if (AbovePotentialActive) {
      AbovePotential.set_HitReturn();
    }
    if (ReboundPotentialActive) {
      ReboundPotential.set_HitReturn();
    }

    // Recover the active bits for the old call level
    restore_status_bits(old_active_bits);
  };

  void activate_entry(bool br_taken) {
    // Update the AR bit
    if (BelowPotentialActive) {
      if (LastBranchTaken) BelowPotential.clear_ARTaken();
      else BelowPotential.clear_ARNTaken();
    }
    if (AbovePotentialActive) {
      if (LastBranchTaken) AbovePotential.clear_ARTaken();
      else AbovePotential.clear_ARNTaken();
    }
    if (ReboundPotentialActive) {
      if (LastBranchTaken) ReboundPotential.clear_ARTaken();
      else ReboundPotential.clear_ARNTaken();
    }

    LastBranchTaken = br_taken;
    BelowPotentialActive = true;
    AbovePotentialActive = true;
    ReboundPotentialActive = true;
    BelowPotentialReached = false;
    AbovePotentialReached = false;
    ReboundPotentialReached = false;
  }

  void train_entry(uint64_t commit_pc) {
    /*
     * When a branch is executed for the first time, initialize the BelowPotential to the sequentially next PC
     * following the branch.
     *
     * If the BelowPotential PC is committed, the BelowPotential becomes inactive.
     *
     * Else if a PC below the BelowPotential is committed, update the BelowPotential to the committed PC,
     * and the BelowPotential becomes inactive.
     */
    if (BelowPotentialActive) {
      if (BelowPotential.get_potential_pc() == commit_pc) reach_BelowPotential();
      else if (BelowPotential.get_potential_pc() < commit_pc) update_BelowPotential(commit_pc);
    }

    /*
     * The AbovePotential is trained similarly to the BelowPotential, except it is only updated in response to
     * PCs committed which are above the targeted branch and below the current AbovePotential value.
     *
     * When a branch is first executed, the AbovePotential is initialized to an invalid value, and will be updated
     * by the PC of the first instruction executed above the targeted branch.
     */
    if (AbovePotentialActive && (commit_pc < BranchPC)) {
      if (!AbovePotential.test_potential_pc_valid() || (AbovePotential.get_potential_pc() < commit_pc))
        update_AbovePotential(commit_pc);
      else if (AbovePotential.get_potential_pc() == commit_pc) reach_AbovePotential();
    }

    /*
     *  If the ReboundPotential is ever executed (before or after the BelowPotential), it becomes inactive.
     *  Else, after the BelowPotential has been executed, observe for the execution of any PC:
     *   - below the targeted branch
     *   - below the ReboundPotential
     *   - above the BelowPotential.
     *  If such a PC is committed, update the ReboundPotential, and the ReboundPotential becomes inactive
     */
    if (ReboundPotentialActive) {
      if (ReboundPotential.get_potential_pc() == commit_pc) reach_ReboundPotential();
      else if (
        BelowPotentialReached &&
        (BranchPC < commit_pc) &&
        (ReboundPotential.get_potential_pc() < commit_pc) &&
        (commit_pc < BelowPotential.get_potential_pc()) &&
        (ReboundPotential.get_potential_pc() < commit_pc)
        ) {
        update_ReboundPotential(commit_pc);
      }
    }
  }

  struct prediction_details {
    int reason_id;
    int cat_id;
  };

  uint64_t make_prediction(prediction_details *prediction_details = nullptr) const {
    uint64_t prediction;
    int reason_id, cat_id;
    do {
      // (1) If HitReturn is set for all potential reconvergence PCs, predict the function return.
      reason_id = 1;
      if (BelowPotential.test_HitReturn() && AbovePotential.test_HitReturn() && ReboundPotential.test_HitReturn()) {
        // Use invalid PC-1 to represent return address for now
        cat_id = 0;
        prediction = potential_reconv_point::INVALID_POTENTIAL - 1;
        break;
      }

      // (2) If some reconvergence PC has its reached first flag set, predict that reconvergence PC.
      reason_id = 2;
      if (BelowPotential.test_ReachedFirst()) {
        cat_id = 1;
        prediction = BelowPotential.get_potential_pc();
        break;
      } else if (AbovePotential.test_ReachedFirst()) {
        cat_id = 2;
        prediction = AbovePotential.get_potential_pc();
        break;
      } else if (ReboundPotential.test_ReachedFirst()) {
        cat_id = 3;
        prediction = ReboundPotential.get_potential_pc();
        break;
      }

      // (3) If some reconvergence PC is always reached whether the branch is taken or not taken, predict. that reconvergence PC.
      reason_id = 3;
      if (BelowPotential.test_ARTaken() && BelowPotential.test_ARNTaken()) {
        cat_id = 1;
        prediction = BelowPotential.get_potential_pc();
        break;
      } else if (AbovePotential.test_ARTaken() && AbovePotential.test_ARNTaken()) {
        cat_id = 2;
        prediction = AbovePotential.get_potential_pc();
        break;
      } else if (ReboundPotential.test_ARTaken() && ReboundPotential.test_ARNTaken()) {
        cat_id = 3;
        prediction = ReboundPotential.get_potential_pc();
        break;
      }

      // (4) If some reconvergence PC is always reached when the branch is either taken or else always reached when the branch is not taken, predict that reconvergence PC.
      reason_id = 4;
      if (BelowPotential.test_ARTaken() || BelowPotential.test_ARNTaken()) {
        cat_id = 1;
        prediction = BelowPotential.get_potential_pc();
        break;
      } else if (AbovePotential.test_ARTaken() || AbovePotential.test_ARNTaken()) {
        cat_id = 2;
        prediction = AbovePotential.get_potential_pc();
        break;
      } else if (ReboundPotential.test_ARTaken() || ReboundPotential.test_ARNTaken()) {
        cat_id = 3;
        prediction = ReboundPotential.get_potential_pc();
        break;
      }

      reason_id = 5;
      // (5) Predict the BelowPotential.
      cat_id = 1;
      prediction = BelowPotential.get_potential_pc();
    } while (false);

    if (prediction_details) {
      prediction_details->cat_id = cat_id;
      prediction_details->reason_id = reason_id;
    }

    return prediction;
  }

private:
  uint8_t dump_status_bits() const {
    int
      bp_reached = BelowPotentialReached,
      ap_reached = AbovePotentialReached,
      rp_reached = ReboundPotentialReached,
      bp_active = BelowPotentialActive,
      ap_active = AbovePotentialActive,
      rp_active = ReboundPotentialActive,
      last_taken = LastBranchTaken;

    uint8_t bits = (
      (bp_reached << 6) |
      (ap_reached << 5) |
      (rp_reached << 4) |
      (bp_active << 3) |
      (ap_active << 2) |
      (rp_active << 1) |
      (last_taken << 0)
    );
    return bits;
  };

  void restore_status_bits(uint8_t bits) {
    BelowPotentialReached = bits & (1 << 6);
    AbovePotentialReached = bits & (1 << 5);
    ReboundPotentialReached = bits & (1 << 4);
    BelowPotentialActive = bits & (1 << 3);
    AbovePotentialActive = bits & (1 << 2);
    ReboundPotentialActive = bits & (1 << 1);
    LastBranchTaken = bits & (1 << 0);
  };

  static uint64_t pc_next(uint64_t pc) { return pc + 4; };

  void initialize_BelowPotential() {
    BelowPotential.set_potential_pc(pc_next(BranchPC));

    BelowPotential.clear_HitReturn();
    BelowPotential.set_ARTaken();
    BelowPotential.set_ARNTaken();
    BelowPotential.set_ReachedFirst();
  };

  void initialize_AbovePotential() {
    AbovePotential.invalidate_potential_pc();

    AbovePotential.clear_HitReturn();
    AbovePotential.set_ARTaken();
    AbovePotential.set_ARNTaken();
    AbovePotential.set_ReachedFirst();
  };

  void initialize_ReboundPotential() {
    ReboundPotential.set_potential_pc(pc_next(BranchPC));

    ReboundPotential.clear_HitReturn();
    ReboundPotential.set_ARTaken();
    ReboundPotential.set_ARNTaken();
    ReboundPotential.set_ReachedFirst();
  };

  void set_all_ReachFirst() {
    BelowPotential.set_ReachedFirst();
    AbovePotential.set_ReachedFirst();
    ReboundPotential.set_ReachedFirst();
  };

  void reach_BelowPotential() {
    // Update self's and other's flags
    BelowPotentialActive = false;
    BelowPotentialReached = true;
    if (!AbovePotentialReached && !ReboundPotentialReached) {
      AbovePotential.clear_ReachedFirst();
      ReboundPotential.clear_ReachedFirst();
    }
  };

  void reach_AbovePotential() {
    // Update self's and other's flags
    AbovePotentialActive = false;
    AbovePotentialReached = true;
    if (!BelowPotentialReached && !ReboundPotentialReached) {
      BelowPotential.clear_ReachedFirst();
      ReboundPotential.clear_ReachedFirst();
    }
  };

  void reach_ReboundPotential() {
    // Update self's and other's flags
    ReboundPotentialActive = false;
    ReboundPotentialReached = true;
    if (!BelowPotentialReached && !AbovePotentialReached) {
      BelowPotential.clear_ReachedFirst();
      AbovePotential.clear_ReachedFirst();
    }
  };

  void update_BelowPotential(uint64_t new_potential_pc) {
    // Update Potential Reconvergence Point
    BelowPotential.set_potential_pc(new_potential_pc);
    // Each time after the BelowPotential is updated, the ReboundPotential is given
    // the value of the static instruction following the branch.
    ReboundPotential.set_potential_pc(pc_next(BranchPC));
    // Update self's and other's flags
    BelowPotential.clear_HitReturn();
    BelowPotential.set_ARNTaken();
    BelowPotential.set_ARTaken();
    set_all_ReachFirst();
    BelowPotentialActive = false;
  };

  void update_AbovePotential(uint64_t new_potential_pc) {
    // Update Potential Reconvergence Point
    AbovePotential.set_potential_pc(new_potential_pc);
    // Update self's and other's flags
    AbovePotential.clear_HitReturn();
    AbovePotential.set_ARNTaken();
    AbovePotential.set_ARTaken();
    set_all_ReachFirst();
    AbovePotentialActive = false;
  };

  void update_ReboundPotential(uint64_t new_potential_pc) {
    // Update Potential Reconvergence Point
    ReboundPotential.set_potential_pc(new_potential_pc);
    // Update self's and other's flags
    ReboundPotential.clear_HitReturn();
    ReboundPotential.set_ARNTaken();
    ReboundPotential.set_ARTaken();
    set_all_ReachFirst();
    ReboundPotentialActive = false;
  };

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
  RPT() {
    reset();
  };

  bool contains(uint64_t pc) const {
    return m_RPT_entries.count(pc);
  }

  void activate(uint64_t pc, bool br_taken) {
    if (!contains(pc)) {
      m_RPT_entries[pc] = RPT_entry();
      m_RPT_entries[pc].initialize(pc);
    }
    RPT_entry &entry = m_RPT_entries[pc];
    if (!m_active_record[m_current_call_depth].count(pc)) {
      bool entry_st_bits = entry.increase_call_level();
      m_active_record[m_current_call_depth][pc] = entry_st_bits;
    }
    entry.activate_entry(br_taken);
  }

  void train(uint64_t commit_pc) {
    for (auto &i : m_active_record[m_current_call_depth]) {
      const auto entry_pc = i.first;
      auto &entry = m_RPT_entries[entry_pc];
      m_RPT_entries[entry_pc].train_entry(commit_pc);
    }
  }

  void increase_call_level() {
    /*
     * at times when the main thread executes at a greater call
     * level than the training branch, the RPT entry is temporarily
     * inactivated (but another instance of the same branch may in
     * fact be active and training the RPT entry)
     */
    assert (m_current_call_depth < (LIMIT_MAX_CALL_DEPTH - 1));
    m_current_call_depth++;
    m_active_record[m_current_call_depth].clear();
  }

  void decrease_call_level() {
    /*
     * When control returns to the same call level as the training branch, the entry is
     * again activated. When the function containing an active RPT
     * entry returns, that entry is inactivated.
     */
    assert(0 < m_current_call_depth);
    // Restore the old level's state bits for entries that have been activated in this level
    for (auto &i : m_active_record[m_current_call_depth]) {
      const auto pc = i.first;
      const auto st_bits = i.second;
      m_RPT_entries[pc].decrease_call_level(st_bits);
    }
    m_current_call_depth--;
  };

  void reset() {
    m_current_call_depth = INIT_CALL_DEPTH;
    for (auto &i : m_active_record) {
      i.clear();
    }
  };
};

class BFT {
public:
  struct br_stat {
    uint64_t cnt_taken;
    uint64_t cnt_ntaken;

    br_stat() : cnt_taken(0), cnt_ntaken(0) {};
  };

  br_stat get_stat(uint64_t pc) {
    if (m_branches_history.count(pc))
      return m_branches_history[pc];
    else
      return {};
  }

  bool filter(uint64_t pc) {
    static const uint64_t SAMPLE_THRESHOLD = 1; //30; // only stop filtering if we have sampled at least SAMPLE_THRESHOLD,
    static const double BIAS_THRESHOLD = 1; //0.95;   // and the the bias rate is lower than this

    if (!m_branches_history.count(pc))
      return true; // filter out the pc that never be trained

    br_stat &br_history = m_branches_history[pc];

    uint64_t cnt_major = std::max(br_history.cnt_taken, br_history.cnt_ntaken);
    uint64_t cnt_total = br_history.cnt_taken + br_history.cnt_ntaken;

    return !(
      (SAMPLE_THRESHOLD < cnt_total) &&
      ((double) cnt_major / cnt_total) < BIAS_THRESHOLD
    );
  };

  void train(uint64_t pc, bool branch_taken) {
    if (!m_branches_history.count(pc)) {
      m_branches_history[pc] = {};
    }
    br_stat &br_history = m_branches_history[pc];
    if (branch_taken) br_history.cnt_taken++;
    else br_history.cnt_ntaken++;
  };

  std::map<uint64_t, br_stat> m_branches_history;
};

class reconv_predictor {
private:
  BFT m_BFT;
  RPT m_RPT;
public:
  void on_branch_retired(uint64_t pc, bool outcome) {
    // feed the BFT, and check whether this branch should be filter
    m_BFT.train(pc, outcome);
    if (m_RPT.contains(pc) || !m_BFT.filter(pc))
      // activate the entry, with branch outcome
      m_RPT.activate(pc, outcome);
  };

  void on_indirect_jmp_retired(uint64_t pc) {
    // activate the entry without a always taken branch outcome (always taken)
    m_RPT.activate(pc, true);
  };

  void on_other_insn_retired(uint64_t pc) {
    // train the entry
    m_RPT.train(pc);
  };

  void on_function_call(uint64_t pc, uint64_t target_addr) {
    // increase the call level
    m_RPT.increase_call_level();
  };

  void on_function_return(uint64_t pc, uint64_t return_addr) {
    // decrease the call level
    m_RPT.decrease_call_level();
  };

  std::string dump_RPT_result_csv() {
    static const char *const cat_name[] = {
      "Return",
      "Below",
      "Above",
      "Rebound"
    };
    static const char *const reason_name[] = {
      "[0] Unknown",
      "[1] All hit return",
      "[2] Reach first",
      "[3] Always reach whether taken or not taken",
      "[4] Always reach only taken or not taken",
      "[5] Fallback to BelowPotential"
    };
    std::stringstream output_stream;
    output_stream << "Branch,ReconvPoint,TakenCnt,NTakenCnt,RecCat,Reason" << std::endl;
    for (auto &i: m_RPT.m_RPT_entries) {
      const auto entry_pc = i.first;
      const auto &entry = i.second;
      const auto entry_stat = m_BFT.get_stat(entry_pc);
      RPT_entry::prediction_details pred_reason = {};
      const auto reconv_pc = entry.make_prediction(&pred_reason);


      output_stream << "0x" << std::setw(16) << std::setfill('0') << std::setbase(16) << entry_pc;
      output_stream << ",";
      output_stream << "0x" << std::setw(16) << std::setfill('0') << std::setbase(16) << reconv_pc;
      output_stream << ",";
      output_stream << std::setw(8) << std::setfill(' ') << std::setbase(10) << entry_stat.cnt_taken;
      output_stream << ",";
      output_stream << std::setw(8) << std::setfill(' ') << std::setbase(10) << entry_stat.cnt_ntaken;
      output_stream << ",";
      output_stream << std::setw(8) << std::setfill(' ') << cat_name[pred_reason.cat_id];
      output_stream << ",";
      output_stream << reason_name[pred_reason.reason_id];
      output_stream << std::endl;
    }

    return output_stream.str();
  };

  std::string dump_BFT_result_csv() {
    std::stringstream output_stream;
    output_stream << "Branch,TakenCnt,NTakenCnt,Filtered" << std::endl;
    for (auto &i: m_BFT.m_branches_history) {
      const auto entry_pc = i.first;
      const auto &entry_stat = m_BFT.get_stat(entry_pc);
      output_stream << "0x" << std::setw(16) << std::setfill('0') << std::setbase(16) << entry_pc;
      output_stream << ",";
      output_stream << std::setw(8) << std::setfill(' ') << std::setbase(10) << entry_stat.cnt_taken;
      output_stream << ",";
      output_stream << std::setw(8) << std::setfill(' ') << std::setbase(10) << entry_stat.cnt_ntaken;
      output_stream << ",";
      output_stream << std::boolalpha << m_BFT.filter(entry_pc);
      output_stream << std::endl;
    }

    return output_stream.str();
  };

};


#endif //RISCV_ISA_SIM_RECONV_PREDICTOR_H
