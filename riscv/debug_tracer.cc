#include "config.h"

#ifdef RISCV_ENABLE_DBG_TRACE

#ifdef RISCV_ENABLE_COMMITLOG
#error Configure flag --enable-commitlog is conflict with --enable-dbg-trace
#endif

#include <cstring>
#include <cassert>
#include <cinttypes>
#include "debug_tracer.h"
#include "mmu.h"

/************* Main Tracer *************/
debug_tracer_t::debug_tracer_t(processor_t &target_processor)
    : m_insn_seq(0),
      m_instret(0),
      m_enabling_instret(UINT64_MAX),
      m_enabled(false),
      m_tgt_proc(target_processor),
      m_rec_insn() {
  memset(&m_rec_insn, 0, sizeof(m_rec_insn));
}

void debug_tracer_t::enable_trace(uint64_t skip_amount) {
  m_instret = m_tgt_proc.get_state()->count;
  m_enabling_instret = m_instret + skip_amount;
}

void debug_tracer_t::disable_trace() {
  m_enabling_instret = UINT64_MAX;
}

void debug_tracer_t::register_trace_output(std::unique_ptr<trace_output_t> trace_output) {
  if (trace_output) m_trace_output.emplace_back(std::move(trace_output));
}


void debug_tracer_t::trace_before_insn_ic_fetch(reg_t pc) {
  if (!(m_enabled = m_enabling_instret <= m_instret)) {
    return;
  }

  m_rec_insn.pc = pc;
  m_rec_insn.next_pc = 0;
  m_rec_insn.seqno = m_insn_seq;
  m_rec_insn.cycle = m_insn_seq; // Assume one trace per cycle in ISA simulator
  m_rec_insn.instret = m_instret;
  m_rec_insn.valid = true;
}

void debug_tracer_t::trace_before_insn_execute(reg_t pc, insn_t insn) {
  if (!m_enabled)
    return;

  if (!m_rec_insn.valid) {
    trace_before_insn_ic_fetch(pc);
  }
  assert(m_rec_insn.valid);
  assert(m_rec_insn.pc == pc);

  m_rec_insn.insn = insn;
  m_rec_insn.good = true;
}

void debug_tracer_t::trace_after_insn_execute(reg_t pc, reg_t next_pc) {
  if (!m_enabled)
    return;

  assert(m_rec_insn.valid);
  assert(m_rec_insn.pc == pc);

  m_rec_insn.next_pc = next_pc;
  m_rec_insn.post_exe_state = *m_tgt_proc.get_state();

  drain_curr_record();
}

void debug_tracer_t::trace_after_take_trap(trap_t &t, reg_t epc, reg_t new_pc) {
  if (!m_enabled)
    return;
  if (m_rec_insn.valid) {
    // the trap is caused by an instruction (sync exception)
    assert(m_rec_insn.pc == epc);
    m_rec_insn.next_pc = new_pc;
    m_rec_insn.post_exe_state = *m_tgt_proc.get_state();
    m_rec_insn.exception = true;
    drain_curr_record();
  } else {
    // the trap is caused by an external interrupt signal (async interrupt)
    insn_t null_insi(0);
    clear_curr_record();
    // to log this event, an artificial instruction is inserted
    // to distinguish it from the real, core fetched instruction, pc is set to all 1 and instruction is set to NULL
    m_rec_insn.pc = -1;
    m_rec_insn.next_pc = new_pc;
    m_rec_insn.insn = null_insi;
    m_rec_insn.good = false;
    m_rec_insn.valid = true;
    m_rec_insn.seqno = m_insn_seq;
    m_rec_insn.cycle = m_insn_seq;
    m_rec_insn.instret = m_instret;
    m_rec_insn.post_exe_state = *m_tgt_proc.get_state();
    m_rec_insn.exception = true;
    drain_curr_record();
  }
}

void debug_tracer_t::trace_after_xpr_access(size_t rn, reg_t val, operand_t operand) {
  if (!m_enabled)
    return;

  assert(m_rec_insn.valid && m_rec_insn.good);

  switch (operand) {
    case RSRC1_OPERAND:
    case RSRC2_OPERAND:
    case RSRC3_OPERAND:
      assert(!m_rec_insn.rs_rec[operand].valid || m_rec_insn.rs_rec[operand].val.xval == val);
      m_rec_insn.rs_rec[operand].n = rn;
      m_rec_insn.rs_rec[operand].val.xval = val;
      m_rec_insn.rs_rec[operand].valid = true;
      m_rec_insn.rs_rec[operand].fpr = false;
      break;
    case RDST_OPERAND:
      assert(!m_rec_insn.rd_rec[operand - RDST_OPERAND].valid);
      m_rec_insn.rd_rec[operand - RDST_OPERAND].n = rn;
      m_rec_insn.rd_rec[operand - RDST_OPERAND].val.xval = val;
      m_rec_insn.rd_rec[operand - RDST_OPERAND].valid = true;
      m_rec_insn.rd_rec[operand - RDST_OPERAND].fpr = false;
      break;
    default:
      assert(0);
      break;
  }
}

void debug_tracer_t::trace_after_fpr_access(size_t rn, freg_t val, operand_t operand) {
  if (!m_enabled)
    return;

  assert(m_rec_insn.valid && m_rec_insn.good);

  switch (operand) {
    case RSRC1_OPERAND:
    case RSRC2_OPERAND:
    case RSRC3_OPERAND:
      assert(!m_rec_insn.rs_rec[operand].valid || m_rec_insn.rs_rec[operand].val.fval == val);
      m_rec_insn.rs_rec[operand].n = rn;
      m_rec_insn.rs_rec[operand].val.fval = val;
      m_rec_insn.rs_rec[operand].valid = true;
      m_rec_insn.rs_rec[operand].fpr = true;
      break;
    case RDST_OPERAND:
      assert(!m_rec_insn.rd_rec[operand - RDST_OPERAND].valid);
      m_rec_insn.rd_rec[operand - RDST_OPERAND].n = rn;
      m_rec_insn.rd_rec[operand - RDST_OPERAND].val.fval = val;
      m_rec_insn.rd_rec[operand - RDST_OPERAND].valid = true;
      m_rec_insn.rd_rec[operand - RDST_OPERAND].fpr = true;
      break;
    default:
      assert(0);
      break;
  }
}

void debug_tracer_t::trace_before_dc_translate(reg_t vaddr, size_t size, bool write) {
  if (!m_enabled)
    return;

  assert(m_rec_insn.valid && m_rec_insn.good);

  m_rec_insn.mem_rec.vaddr = vaddr;
  m_rec_insn.mem_rec.op_size = size;
  m_rec_insn.mem_rec.write = write;
  m_rec_insn.mem_rec.valid = true;
}

void debug_tracer_t::trace_after_dc_access(reg_t vaddr, reg_t paddr, freg_t val, size_t size, bool write) {
  if (!m_enabled)
    return;

  assert(
    m_rec_insn.valid &&
    m_rec_insn.good &&
    m_rec_insn.mem_rec.valid &&
    m_rec_insn.mem_rec.vaddr == vaddr &&
    m_rec_insn.mem_rec.write == write &&
    m_rec_insn.mem_rec.op_size == size
  );

  m_rec_insn.mem_rec.val = val;
  m_rec_insn.mem_rec.paddr = paddr;
  m_rec_insn.mem_rec.good = true;
}


void debug_tracer_t::drain_curr_record() {
  if (m_rec_insn.valid) {
    for (auto &output: m_trace_output)
      output->issue_insn(m_rec_insn);
    seqno_incr();
  }
  clear_curr_record();
}


void debug_tracer_t::clear_curr_record() {
  memset(&m_rec_insn, 0, sizeof(m_rec_insn));
}

void debug_tracer_t::seqno_incr() {
  ++m_insn_seq;
}

const insn_record_t &debug_tracer_t::get_current_insn_info() {
  return m_rec_insn;
}

/************* Trace Output *************/
void trace_last_n_wrapper_t::issue_insn(const insn_record_t &insn) {
  insn_rec_circ_buf_push(insn);
}

trace_last_n_wrapper_t::~trace_last_n_wrapper_t() {
  for (
    insn_record_t *p = insn_rec_circ_buf_pop();
    p != nullptr;
    p = insn_rec_circ_buf_pop()) {
    m_wrapped_output->issue_insn(*p);
  }
}

#endif /* RISCV_ENABLE_DBG_TRACE */
