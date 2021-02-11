//
// Created by s117 on 1/28/21.
//

#ifndef RISCV_ISA_SIM_INSN_DEP_TRACKER_H
#define RISCV_ISA_SIM_INSN_DEP_TRACKER_H

#include <set>
#include <list>
#include <cassert>
#include "debug_tracer.h"
#include "reconv_predictor.h"

#define CFS_ENABLE_INVALID_RECONV_MERGE

typedef uint64_t data_addr_t;
typedef uint64_t insn_addr_t;
typedef size_t access_size_t;

class mem_poisoning_tracker {
public:
  struct node_stat_t {
    std::set<insn_addr_t> producer_pc_set;
    size_t ref_cnt;
  };

  typedef std::list<node_stat_t>::iterator node_stat_ptr_t;
private:

  std::list<node_stat_t> m_node_stat_storage;

  std::map<data_addr_t, node_stat_ptr_t> m_mem_dep_tracker;

  // Return whether the given node is still valid upon return
  bool mem_node_ref_dec(node_stat_ptr_t &node_stat) {
    if (--node_stat->ref_cnt == 0) {
      m_node_stat_storage.erase(node_stat);
      return false;
    }
    return true;
  }

  node_stat_ptr_t mem_node_alloc(size_t init_ref_cnt = 1) {
    assert(init_ref_cnt > 0);
    m_node_stat_storage.push_front({});
    auto new_node_stat = m_node_stat_storage.begin();
    new_node_stat->ref_cnt = init_ref_cnt;
    return new_node_stat;
  }

public:

  void clean(data_addr_t addr, access_size_t size) {
    auto dmem_meta = m_mem_dep_tracker.lower_bound(addr);
    while (dmem_meta != m_mem_dep_tracker.end() && dmem_meta->first < addr + size) {
      mem_node_ref_dec(dmem_meta->second);
      auto addr_to_erase = dmem_meta->first;
      dmem_meta++;
      m_mem_dep_tracker.erase(addr_to_erase);
    }
  }

  void poisoning(data_addr_t addr, access_size_t size, const std::set<insn_addr_t> &producer_pc_set) {
    clean(addr, size);
    auto new_node_ptr = mem_node_alloc(size);
    new_node_ptr->producer_pc_set = producer_pc_set;
    for (auto dmem_pos = addr; dmem_pos < addr + size; dmem_pos++) {
      m_mem_dep_tracker[dmem_pos] = new_node_ptr;
    }
  };

  std::set<insn_addr_t> query_producer(data_addr_t addr, access_size_t size) {
    std::set<insn_addr_t> producer_set;
    node_stat_t *last_stat = nullptr;
    for (
      auto dmem_meta = m_mem_dep_tracker.lower_bound(addr);
      dmem_meta != m_mem_dep_tracker.end() && dmem_meta->first < addr + size;
      dmem_meta++
      ) {
      node_stat_t *curr_stat = &(*dmem_meta->second);
      if (last_stat != curr_stat) {
        // as an effort trying to only union unique producer set to the result set
        producer_set.insert(curr_stat->producer_pc_set.begin(), curr_stat->producer_pc_set.end());
        last_stat = curr_stat;
      }
    }
    return producer_set;
  }

  bool is_poisoned(data_addr_t addr, access_size_t size) {
    auto dmem_meta = m_mem_dep_tracker.lower_bound(addr);
    return dmem_meta != m_mem_dep_tracker.end() && dmem_meta->first < addr + size;
  }

  void reset() {
    m_mem_dep_tracker.clear();
    m_node_stat_storage.clear();
  }
};

class reg_poisoning_tracker {
  typedef std::set<insn_addr_t> producer_pc_set_t;
private:
  std::vector<producer_pc_set_t> m_reg_producer_set;
public:
  explicit reg_poisoning_tracker(size_t n_reg) {
    m_reg_producer_set.resize(n_reg);
    for (auto &m : m_reg_producer_set) {
      m.clear();
    }
  }

  void clean(size_t reg_no) {
    m_reg_producer_set[reg_no].clear();
  }

  void poisoning(size_t reg_no, const std::set<insn_addr_t> &producer_pc_set) {
    m_reg_producer_set[reg_no] = producer_pc_set;
  }

  std::set<insn_addr_t> query_producer(size_t reg_no) {
    return m_reg_producer_set[reg_no];
  }

  bool is_poisoned(size_t reg_no) {
    return !m_reg_producer_set[reg_no].empty();
  }

  void reset() {
    for (auto &m : m_reg_producer_set) {
      m.clear();
    }
  }
};


class insn_poisoning_tracker {
public:
  enum tracking_mode_t {
    POISONING,
    PROPAGATE,
    STOP
  } m_tracking_mode;
private:
  static const int N_INT_REG = 32;
  static const int N_FP_REG = 32;

  mem_poisoning_tracker m_mem_tracker;
  // The first N_INT_REG slot is for integer register
  // Then the remaining N_FP_REG slot is for FP register
  reg_poisoning_tracker m_regs_tracker;


public:
  insn_poisoning_tracker() : m_mem_tracker(), m_regs_tracker(N_INT_REG + N_FP_REG) {
    m_tracking_mode = STOP;
  }

  void update(const insn_record_t &insn) {
    switch (m_tracking_mode) {
      case POISONING: {
        auto insn_producer = get_insn_src_producer_set(insn);
        insn_producer.insert(insn.pc);
        set_insn_dst_producer_set(insn, insn_producer);
        break;
      }

      case PROPAGATE: {
        if (get_insn_src_poisoned(insn)) {
          auto insn_producer = get_insn_src_producer_set(insn);
          insn_producer.insert(insn.pc);
          set_insn_dst_producer_set(insn, insn_producer);
        }
        break;
      }

      case STOP:
      default:
        break;
    }
  };

  tracking_mode_t get_tracking_mode() { return m_tracking_mode; };

  void enter_poisoning_mode() { m_tracking_mode = POISONING; };

  void enter_propagate_mode() { m_tracking_mode = PROPAGATE; };

  void stop_tracking() { m_tracking_mode = STOP; };

  bool get_insn_src_poisoned(const insn_record_t &insn) {
    for (const auto &rs_rec : insn.rs_rec) {
      if (rs_rec.valid) {
        size_t reg_offset = rs_rec.fpr ? N_INT_REG : 0;
        if (m_regs_tracker.is_poisoned(reg_offset + rs_rec.n))
          return true;
      }
    }
    if (insn.mem_rec.valid && !insn.mem_rec.write) {
      if (m_mem_tracker.is_poisoned(insn.mem_rec.vaddr, insn.mem_rec.op_size))
        return true;
    }
    return false;
  }

  std::set<insn_addr_t> get_insn_src_producer_set(const insn_record_t &insn) {
    std::set<insn_addr_t> producer_set;
    for (const auto &rs_rec : insn.rs_rec) {
      if (rs_rec.valid) {
        size_t reg_offset = rs_rec.fpr ? N_INT_REG : 0;
        auto rsrc_p_set = m_regs_tracker.query_producer(reg_offset + rs_rec.n);
        if (!rsrc_p_set.empty()) producer_set.insert(rsrc_p_set.begin(), rsrc_p_set.end());
      }
    }
    if (insn.mem_rec.valid && !insn.mem_rec.write) {
      auto msrc_p_set = m_mem_tracker.query_producer(insn.mem_rec.vaddr, insn.mem_rec.op_size);
      if (!msrc_p_set.empty()) producer_set.insert(msrc_p_set.begin(), msrc_p_set.end());
    }

    return producer_set;
  }

  void set_insn_dst_producer_set(const insn_record_t &insn, const std::set<insn_addr_t> &insn_src_producer_set) {
    for (const auto &rd_rec : insn.rd_rec) {
      if (rd_rec.valid) {
        size_t reg_offset = rd_rec.fpr ? N_INT_REG : 0;
        m_regs_tracker.poisoning(rd_rec.n, insn_src_producer_set);
      }
    }
    if (insn.mem_rec.valid && insn.mem_rec.write) {
      m_mem_tracker.poisoning(insn.mem_rec.vaddr, insn.mem_rec.op_size, insn_src_producer_set);
    }
  }

  void reset() {
    m_mem_tracker.reset();
    m_regs_tracker.reset();
  }
};

class control_flow_stack {
public:
  struct cfs_frame {
    ssize_t call_depth;
    insn_addr_t reconv_pc;
    size_t n_cd_retired;
    size_t branch_stack_top; // inclusive
    size_t branch_stack_bottom; // inclusive
  };
private:
  static const size_t DEFAULT_STACK_SIZE = 64;
  static const size_t CFS_CD_REGION_MAX = 128;
  ssize_t m_curr_call_depth;
  std::vector<insn_addr_t> m_active_branch_stack;
  std::vector<cfs_frame> m_cfs;

  control_flow_stack() {
    m_curr_call_depth = 0;
    m_cfs.resize(DEFAULT_STACK_SIZE);
    m_active_branch_stack.resize(DEFAULT_STACK_SIZE);
  };

  void reset() {
    m_curr_call_depth = 0;
    m_cfs.clear();
    m_active_branch_stack.clear();
  }

  void increase_call_depth() {
    ++m_curr_call_depth;
  };

  void decrease_call_depth() {
    --m_curr_call_depth;
  };

  // push a branch and its reconv point into cfs
  void push(insn_addr_t br_pc, insn_addr_t reconv_pc) {
    if (m_cfs.empty()) {
      assert(m_active_branch_stack.empty());
      m_active_branch_stack.push_back(br_pc);
      cfs_frame new_cfs_frame = {
        .call_depth = m_curr_call_depth,
        .reconv_pc = reconv_pc,
        .n_cd_retired = 0,
        .branch_stack_top = 0,
        .branch_stack_bottom = 0,
      };
      m_cfs.push_back(new_cfs_frame);
    } else {
      auto &cfs_tos = m_cfs.back();

      if (m_curr_call_depth == cfs_tos.call_depth && reconv_pc == cfs_tos.reconv_pc) {
        /* in the same call depth, if reconv PC is the same, implicitly merge it into the current cfs top frame */
        m_active_branch_stack.push_back(br_pc);
        ++cfs_tos.branch_stack_top;
      } else {
        /* else, create a new CFS frame
         * here we don't need to treat two special reconv PC differently, RECONV_POINT_INVALID and RECONV_POINT_RETURN,
         * as those two PC cannot be met in the normal execution
         */
        auto branch_stack_idx = m_active_branch_stack.size();
        m_active_branch_stack.push_back(br_pc);
        cfs_frame new_cfs_frame = {
          .call_depth = m_curr_call_depth,
          .reconv_pc = reconv_pc,
          .n_cd_retired = 0,
          .branch_stack_top = branch_stack_idx,
          .branch_stack_bottom = branch_stack_idx,
        };
        m_cfs.push_back(new_cfs_frame);
      }
    }
  }

  // pop the top frame from CFS
  void pop() {
    if (m_cfs.empty())
      return;
    auto &cfs_tos = m_cfs.back();
    // pop record from the active branch stack
    auto branch_to_pop = cfs_tos.branch_stack_top - cfs_tos.branch_stack_bottom + 1;
    for (auto i = branch_to_pop; i >= 0; --i) {
      m_active_branch_stack.pop_back();
    }
    m_cfs.pop_back();
  }

  bool check_reconv(uint64_t curr_pc) const {
    if (m_cfs.empty())
      return false;
    const auto &cfs_tos = m_cfs.back();
    return (m_curr_call_depth == cfs_tos.call_depth && curr_pc == cfs_tos.reconv_pc);
  }

  bool good() const {
    if (m_cfs.empty()) {
      assert(m_active_branch_stack.empty());
      return true;
    }

    const auto &cfs_tos = m_cfs.back();
    if (cfs_tos.n_cd_retired > CFS_CD_REGION_MAX) {
      // the reconv pc is not seen before reaching the max Cd region threshold
      return false;
    }

    if (m_curr_call_depth < cfs_tos.call_depth) {
      // the reconv pc is not seen before function return
      return false;
    }

    return true;
  }

  const std::vector<insn_addr_t> &get_active_branches() const { return m_active_branch_stack; };

  void on_insn_retire(insn_addr_t insn_pc) {
    if (m_cfs.empty())
      return;
    auto &cfs_tos = m_cfs.back();
    ++cfs_tos.n_cd_retired;
    if (check_reconv(insn_pc)) {
      pop();
    }
  };

  void on_branch(insn_addr_t br_pc, insn_addr_t reconv_pc) {
    on_insn_retire(br_pc);

#ifdef CFS_ENABLE_INVALID_RECONV_MERGE
    if (m_cfs.empty()) {
      push(br_pc, reconv_pc);
    } else {
      auto cfs_top = m_cfs.back();
      if (m_curr_call_depth == cfs_top.call_depth && cfs_top.reconv_pc == RECONV_POINT_INVALID) {
        push(br_pc, cfs_top.reconv_pc);
      } else {
        push(br_pc, reconv_pc);
      }
    }
#else
    push(br_pc, reconv_pc);
#endif

  };

  void on_fcall(uint64_t fcall_pc) {
    on_insn_retire(fcall_pc);
    increase_call_depth();
  }

  void on_fret(uint64_t fret_pc) {
    on_insn_retire(fret_pc);
    if (check_reconv(RECONV_POINT_RETURN)) {
      pop();
    }
    decrease_call_depth();
  }

  void on_other(uint64_t insn_pc) {
    on_insn_retire(insn_pc);
  }
};

#endif //RISCV_ISA_SIM_INSN_DEP_TRACKER_H
