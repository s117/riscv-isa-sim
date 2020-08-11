#ifndef __DEBUG_TRACER_H
#define __DEBUG_TRACER_H

#include "config.h"

#ifdef RISCV_ENABLE_DBG_TRACE

#include <stddef.h>
#include <string>
#include <iostream>
//#include "processor.h"
#include "trap.h"
#include "gzstream.h"
#include "disasm.h"
#include "mmu.h"

typedef struct {
  bool valid;
  bool fpr;

  size_t n;
  union {
    reg_t xval;
    freg_t fval;
  } val;
} reg_record_t;

typedef struct {
  bool valid;
  bool good;
  bool write;

  reg_t vaddr;
  uint64_t val;
  size_t op_size;
} mem_record_t;

const static size_t MAX_RDST = 1;
const static size_t MAX_RSRC = 3;

typedef struct {
  bool valid;
  bool good;
  bool exception;

  reg_t pc;
  insn_t insn;
  uint64_t seqno;

  reg_record_t rs_rec[MAX_RSRC];
  reg_record_t rd_rec[MAX_RDST];

  mem_record_t mem_rec;


} insn_record_t;

class processor_t;

class dbg_tracer_hook_mmu_t;

class debug_tracer_t {
public:
  explicit debug_tracer_t(processor_t *target_processor);

  virtual ~debug_tracer_t();

  void enable_trace();

  void trace_before_insn_ic_fetch(reg_t pc);

  void trace_before_insn_execute(reg_t pc, insn_t insn);

  void trace_after_insn_execute(reg_t pc);

  void trace_after_xpr_access(size_t rn, reg_t val, operand_t operand);

  void trace_after_fpr_access(size_t rn, freg_t val, operand_t operand);

  void trace_before_dc_translate(reg_t vaddr, bool write);

  void trace_after_dc_access(reg_t vaddr, freg_t val, size_t size, bool write);

  void trace_after_take_trap(trap_t &t, reg_t epc, reg_t new_pc);

  bool enabled() { return m_enabled; };

private:
  void issue_curr_record();

  void clear_curr_record();

  uint64_t next_seqno();

  disassembler_t m_dismer;
  processor_t *m_tgt_proc;

  bool m_enabled;
//  std::ofstream m_tr_ostream;
  ogzstream m_tr_ostream;

  insn_record_t m_rec_insn;
  uint64_t m_insn_seq;
};

class dbg_tracer_hook_mmu_t : public mmu_t {
public:
  dbg_tracer_hook_mmu_t(processor_t *upstream_processor, mmu_t *downstream_mmu);

  virtual ~dbg_tracer_hook_mmu_t() = default;

  // template for functions that load an aligned value from memory
#define dbg_tracer_hook_load_func(type) \
    __MMU_VIRTUAL type##_t load_##type(reg_t addr) __MMU_DIRECTIVE_ALWAYS_INLINE { \
      m_tracer_to_report->trace_before_dc_translate(addr, false); \
      auto load_val = m_downstream_mmu->load_##type(addr); \
      m_tracer_to_report->trace_after_dc_access(addr, load_val, sizeof(load_val), false); \
      return load_val; \
    }
  // template for functions that store an aligned value to memory
#define dbg_tracer_hook_store_func(type) \
    __MMU_VIRTUAL void store_##type(reg_t addr, type##_t val) { \
      m_tracer_to_report->trace_before_dc_translate(addr, true); \
      m_downstream_mmu->store_##type(addr, val); \
      m_tracer_to_report->trace_after_dc_access(addr, val, sizeof(val), true); \
    }

  // load value from memory at aligned address; zero extend to register width
  dbg_tracer_hook_load_func(uint8)

  dbg_tracer_hook_load_func(uint16)

  dbg_tracer_hook_load_func(uint32)

  dbg_tracer_hook_load_func(uint64)

  // load value from memory at aligned address; sign extend to register width
  dbg_tracer_hook_load_func(int8)

  dbg_tracer_hook_load_func(int16)

  dbg_tracer_hook_load_func(int32)

  dbg_tracer_hook_load_func(int64)

  // store value to memory at aligned address
  dbg_tracer_hook_store_func(uint8)

  dbg_tracer_hook_store_func(uint16)

  dbg_tracer_hook_store_func(uint32)

  dbg_tracer_hook_store_func(uint64)

  __MMU_VIRTUAL icache_entry_t *access_icache(reg_t addr) __MMU_DIRECTIVE_ALWAYS_INLINE
  {
    m_tracer_to_report->trace_before_insn_ic_fetch(addr);
    auto ic_entry = m_downstream_mmu->access_icache(addr);
    return ic_entry;
  }

protected:
  mmu_t *m_downstream_mmu;
  processor_t *m_upstream_processor;
  debug_tracer_t *m_tracer_to_report;
};

#endif /* RISCV_ENABLE_DBG_TRACE */

#endif /* __DEBUG_TRACER_H */
