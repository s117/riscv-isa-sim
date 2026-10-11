#ifndef __DEBUG_TRACER_H
#define __DEBUG_TRACER_H

#include "config.h"

#ifdef RISCV_ENABLE_DBG_TRACE

#include <cstddef>
#include <string>
#include <iostream>
#include <memory>
#include "trap.h"
#include "fesvr/gzstream.h"
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
  reg_t paddr;
  uint64_t val;
  size_t op_size;
} mem_record_t;

const static size_t MAX_RDST = 1;
const static size_t MAX_RSRC = 3;

// The parts of a record a trace output asks for, see trace_output_t::record_fields(). The record header (valid, good,
// exception, pc, next_pc, insn, seqno, cycle and instret) is always recorded.
const static unsigned TRACE_OPERANDS = 1u << 0;       // rs_rec and rd_rec
const static unsigned TRACE_MEMORY = 1u << 1;         // mem_rec
const static unsigned TRACE_POST_EXE_STATE = 1u << 2; // post_exe_state of every record, not only of trap records
const static unsigned TRACE_DEFAULT_FIELDS = TRACE_OPERANDS | TRACE_MEMORY;

typedef struct {
  bool valid;
  bool good;
  bool exception;

  reg_t pc;
  reg_t next_pc;
  insn_t insn;
  uint64_t seqno;
  uint64_t cycle;
  uint64_t instret;

  reg_record_t rs_rec[MAX_RSRC];
  reg_record_t rd_rec[MAX_RDST];

  mem_record_t mem_rec;

  // The state after the instruction (or the trap). Copying it costs as much as the rest of the record, so it is only
  // valid on records with exception set, and on every record if a registered output asks for TRACE_POST_EXE_STATE.
  state_t post_exe_state;
} insn_record_t;

/************* Trace Output *************/
class trace_output_t {
public:
  virtual ~trace_output_t() = default;

  virtual void issue_insn(const insn_record_t &insn) = 0;

  // The TRACE_* parts of the records this output reads. The tracer records a part if any of its outputs asks for it,
  // so an output that reads less makes tracing faster. Called when the output is registered.
  virtual unsigned record_fields() const { return TRACE_DEFAULT_FIELDS; }
};

class trace_output_null_t : public trace_output_t {
public:
  void issue_insn(const insn_record_t &insn) final {};

  unsigned record_fields() const final { return 0; }
};

class trace_last_n_wrapper_t : public trace_output_t {
public:
  trace_last_n_wrapper_t(size_t n, std::unique_ptr<trace_output_t> wrapped_output)
      : m_insn_rec_circ_buf(n),
        m_sz_buf(n),
        m_tail(0),
        m_head(0),
        m_empty(true),
        m_wrapped_output(std::move(wrapped_output)) {};

  ~trace_last_n_wrapper_t() override;

  void issue_insn(const insn_record_t &insn) override;

  unsigned record_fields() const override { return m_wrapped_output->record_fields(); }

private:
  size_t next_idx(size_t i) const {
    // return (i + 1) % m_sz_buf;
    auto nidx = i + 1;
    return (nidx == m_sz_buf) ? 0 : nidx;
  }

  void insn_rec_circ_buf_push(const insn_record_t &insn_rec) {
    m_insn_rec_circ_buf[m_tail] = insn_rec;

    if (likely(m_tail == m_head)) {
      if (unlikely(m_empty)) {
        m_tail = next_idx(m_tail);
        m_empty = false;
      }
      else {
        m_head = m_tail = next_idx(m_tail);
      }
    }
    else {
      m_tail = next_idx(m_tail);
    }
  }

  insn_record_t *insn_rec_circ_buf_pop() {
    insn_record_t *ret_ptr = nullptr;

    if (!m_empty) {
      ret_ptr = &m_insn_rec_circ_buf[m_head];
      m_head = next_idx(m_head);
      if (m_head == m_tail)
        m_empty = true;
    }

    return ret_ptr;
  }

  std::vector<insn_record_t> m_insn_rec_circ_buf;
  size_t m_sz_buf;
  size_t m_tail; // wr at tail
  size_t m_head; // rd at head
  bool m_empty;

  std::unique_ptr<trace_output_t> m_wrapped_output;
};

/************* Main Tracer *************/
class debug_tracer_t {
public:
  explicit debug_tracer_t(processor_t &target_processor);

  ~debug_tracer_t() = default;

  void enable_trace(uint64_t skip_amount = 0);

  void disable_trace();

  void register_trace_output(std::unique_ptr<trace_output_t> trace_output);

  void clear_trace_output() {
    m_trace_output.clear();
    m_record_fields = 0;
  }

  // The trace_*() functions run for every instruction: they only test m_enabled inline, the recording is out of line.

  void trace_before_insn_ic_fetch(reg_t pc) {
    if (unlikely(m_enabled = m_enabling_instret <= m_instret)) trace_before_insn_ic_fetch_slow(pc);
  }

  void trace_before_insn_execute(reg_t pc, insn_t insn) {
    if (unlikely(m_enabled)) trace_before_insn_execute_slow(pc, insn);
  }

  void trace_after_xpr_access(size_t rn, reg_t val, operand_t operand) {
    if (unlikely(m_enabled)) trace_after_xpr_access_slow(rn, val, operand);
  }

  void trace_after_fpr_access(size_t rn, freg_t val, operand_t operand) {
    if (unlikely(m_enabled)) trace_after_fpr_access_slow(rn, val, operand);
  }

  void trace_before_dc_translate(reg_t vaddr, size_t size, bool write) {
    if (unlikely(m_enabled)) trace_before_dc_translate_slow(vaddr, size, write);
  }

  void trace_after_dc_access(reg_t vaddr, reg_t paddr, freg_t val, size_t size, bool write) {
    if (unlikely(m_enabled)) trace_after_dc_access_slow(vaddr, paddr, val, size, write);
  }

  void trace_after_insn_execute(reg_t pc, reg_t next_pc) {
    if (unlikely(m_enabled)) trace_after_insn_execute_slow(pc, next_pc);
  }

  void trace_after_take_trap(trap_t &t, reg_t epc, reg_t new_pc) {
    if (unlikely(m_enabled)) trace_after_take_trap_slow(t, epc, new_pc);
  }

  // discard the record of the instruction in flight, as it didn't execute and will be fetched again
  void trace_cancel_insn() {
    if (unlikely(m_enabled)) clear_curr_record();
  }

  bool enabled() { return m_enabled; };

  // the flag tested by the trace_*() functions, so that the processor's register accessors can test it inline
  const bool *enabled_flag() const { return &m_enabled; }

  void increment_instret() { ++m_instret; };

  const insn_record_t &get_current_insn_info();

private:
  void trace_before_insn_ic_fetch_slow(reg_t pc);

  void trace_before_insn_execute_slow(reg_t pc, insn_t insn);

  void trace_after_xpr_access_slow(size_t rn, reg_t val, operand_t operand);

  void trace_after_fpr_access_slow(size_t rn, freg_t val, operand_t operand);

  void trace_before_dc_translate_slow(reg_t vaddr, size_t size, bool write);

  void trace_after_dc_access_slow(reg_t vaddr, reg_t paddr, freg_t val, size_t size, bool write);

  void trace_after_insn_execute_slow(reg_t pc, reg_t next_pc);

  void trace_after_take_trap_slow(trap_t &t, reg_t epc, reg_t new_pc);

  void drain_curr_record();

  void clear_curr_record();

  void seqno_incr();

  uint64_t m_insn_seq;
  uint64_t m_instret;

  uint64_t m_enabling_instret;
  bool m_enabled;
  unsigned m_record_fields; // TRACE_* parts asked for by the outputs
  processor_t &m_tgt_proc;
  insn_record_t m_rec_insn;
  std::vector<std::unique_ptr<trace_output_t>> m_trace_output;
};

#endif /* RISCV_ENABLE_DBG_TRACE */

#endif /* __DEBUG_TRACER_H */
