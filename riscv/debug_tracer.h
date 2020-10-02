#ifndef __DEBUG_TRACER_H
#define __DEBUG_TRACER_H

#include "config.h"

#ifdef RISCV_ENABLE_DBG_TRACE
//#define __DBG_TRACE_DEBUG_OUTPUT

#include <cstddef>
#include <string>
#include <iostream>
#include "trap.h"
#include "gzstream.h"
#include "disasm.h"
#include "processor.h"

/************* Trace Record Internal Representation *************/
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
  uint64_t cycle;
  uint64_t instret;

  reg_record_t rs_rec[MAX_RSRC];
  reg_record_t rd_rec[MAX_RDST];

  mem_record_t mem_rec;

  state_t post_exe_state;
} insn_record_t;

/************* Trace Output *************/
class trace_output_t {
public:
  virtual ~trace_output_t() = default;

  virtual void issue_insn(const insn_record_t &insn) = 0;
};

class trace_output_direct_t : public trace_output_t {
public:
  explicit trace_output_direct_t(const std::string &filename_out);

  ~trace_output_direct_t() override;

  void issue_insn(const insn_record_t &insn) override { output_insn_record(insn); };

private:
  void output_insn_record(const insn_record_t &insn);

  disassembler_t m_disassembler;
  std::string m_trace_file_name;
#ifdef __DBG_TRACE_DEBUG_OUTPUT
  std::ofstream m_tr_ostream;
#else
  ogzstream m_tr_ostream;
#endif
};

class trace_output_last_n_t : public trace_output_t {
public:
  trace_output_last_n_t(const std::string &filename_out, size_t n);

  ~trace_output_last_n_t() override;

  void issue_insn(const insn_record_t &insn) override;

private:
  void insn_rec_circ_buf_push(const insn_record_t &insn_rec);

  insn_record_t *insn_rec_circ_buf_pop();

  inline size_t next_idx(size_t i) {
    // return (i + 1) % m_sz_buf;
    auto nidx = i + 1;
    return (nidx == m_sz_buf) ? 0 : nidx;
  };

  insn_record_t *m_insn_rec_circ_buf;
  size_t m_sz_buf;
  size_t m_tail; // wr at tail
  size_t m_head; // rd at head
  bool empty;

  trace_output_direct_t m_direct_output;
};

/************* Main Tracer *************/
class debug_tracer_t {
public:
  explicit debug_tracer_t(processor_t *target_processor);

  virtual ~debug_tracer_t();

  void enable_trace(uint64_t last_n = 0);

  void trace_before_insn_ic_fetch(reg_t pc);

  void trace_before_insn_execute(reg_t pc, insn_t insn);

  void trace_after_xpr_access(size_t rn, reg_t val, operand_t operand);

  void trace_after_fpr_access(size_t rn, freg_t val, operand_t operand);

  void trace_before_dc_translate(reg_t vaddr, bool write);

  void trace_after_dc_access(reg_t vaddr, freg_t val, size_t size, bool write);

  void trace_after_insn_execute(reg_t pc);

  void trace_after_take_trap(trap_t &t, reg_t epc, reg_t new_pc);

  bool enabled() { return m_enabled; };

  void increment_instret() { ++m_instret; };

private:
  void drain_curr_record();

  void clear_curr_record();

  void seqno_incr();

  uint64_t m_insn_seq;
  uint64_t m_instret;

  bool m_enabled;
  processor_t *m_tgt_proc;
  insn_record_t m_rec_insn;
  trace_output_t *m_trace_output;
};

#endif /* RISCV_ENABLE_DBG_TRACE */

#endif /* __DEBUG_TRACER_H */
