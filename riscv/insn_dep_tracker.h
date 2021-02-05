//
// Created by s117 on 1/28/21.
//

#ifndef RISCV_ISA_SIM_INSN_DEP_TRACKER_H
#define RISCV_ISA_SIM_INSN_DEP_TRACKER_H

#include <set>
#include <list>
#include <cassert>
#include "debug_tracer.h"

class mem_poisoning_tracker {
public:
  typedef uint64_t data_addr_t;
  typedef uint64_t inst_addr_t;
  typedef uint8_t access_size_t;
  struct node_stat_t {
    std::set<inst_addr_t> producer_pc_set;
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

  void poisoning(data_addr_t addr, access_size_t size, const std::set<inst_addr_t> &producer_pc_set) {
    clean(addr, size);
    auto new_node_ptr = mem_node_alloc(size);
    new_node_ptr->producer_pc_set = producer_pc_set;
    for (auto dmem_pos = addr; dmem_pos < addr + size; dmem_pos++) {
      m_mem_dep_tracker[dmem_pos] = new_node_ptr;
    }
  };

  std::set<inst_addr_t> query_producer(data_addr_t addr, access_size_t size) {
    std::set<inst_addr_t> producer_set;
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
};

class reg_poisoning_tracker {
  typedef uint64_t inst_addr_t;
  typedef std::set<inst_addr_t> producer_pc_set_t;
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

  void poisoning(size_t reg_no, const std::set<inst_addr_t> &producer_pc_set) {
    m_reg_producer_set[reg_no] = producer_pc_set;
  }

  std::set<inst_addr_t> query_producer(size_t reg_no) {
    return m_reg_producer_set[reg_no];
  }

  bool is_poisoned(size_t reg_no) {
    return !m_reg_producer_set[reg_no].empty();
  }

};

class insn_dep_tracker {
public:
  void enter_poisoning_mode();

  void enter_propagate_mode();

  void import_poisoning_state();

  void export_poisoning_state();

  void update(const insn_record_t &insn);
};


#endif //RISCV_ISA_SIM_INSN_DEP_TRACKER_H
