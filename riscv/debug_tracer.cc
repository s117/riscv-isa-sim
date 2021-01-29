#include "config.h"

#ifdef RISCV_ENABLE_DBG_TRACE

#ifdef RISCV_ENABLE_COMMITLOG
#error Configure flag --enable-commitlog is conflict with --enable-dbg-trace
#endif

#include <cstring>
#include <cassert>
#include <cinttypes>
#include <csignal>
#include <cinttypes>
#include "debug_tracer.h"
#include "mmu.h"

/************* Main Tracer *************/
debug_tracer_t::debug_tracer_t(processor_t *target_processor) : m_rec_insn() {
  memset(&m_rec_insn, 0, sizeof(m_rec_insn));
  m_tgt_proc = target_processor;
  m_enabled = false;
  m_insn_seq = 0;
  m_instret = 0;
  m_trace_output = nullptr;
}

debug_tracer_t::~debug_tracer_t() {
  if (m_enabled) {
    delete m_trace_output;
  }
}

void debug_tracer_t::enable_trace(uint64_t last_n) {
#ifdef __DBG_TRACE_DEBUG_OUTPUT
  std::string trace_file_name = std::string("trace_proc_") + std::to_string(m_tgt_proc->get_id()) + ".txt";
#else
  std::string trace_file_name = std::string("trace_proc_") + std::to_string(m_tgt_proc->get_id()) + ".gz";
#endif
  if (last_n != 0) {
    m_trace_output = new trace_output_last_n_t(trace_file_name, last_n);
  } else {
    m_trace_output = new trace_output_direct_t(trace_file_name);
  }
  m_instret = m_tgt_proc->get_state()->count;
  m_enabled = true;
}

void debug_tracer_t::trace_before_insn_ic_fetch(reg_t pc) {
  if (!m_enabled)
    return;

  m_rec_insn.pc = pc;
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

void debug_tracer_t::trace_after_insn_execute(reg_t pc) {
  if (!m_enabled)
    return;

  assert(m_rec_insn.valid);
  assert(m_rec_insn.pc == pc);

  m_rec_insn.post_exe_state = *m_tgt_proc->get_state();

  drain_curr_record();
}

void debug_tracer_t::trace_after_take_trap(trap_t &t, reg_t epc, reg_t new_pc) {
  if (!m_enabled)
    return;
  if (m_rec_insn.valid) {
    // the trap is caused by an instruction (sync exception)
    assert(m_rec_insn.pc == epc);
    m_rec_insn.post_exe_state = *m_tgt_proc->get_state();
    m_rec_insn.exception = true;
    drain_curr_record();
  } else {
    // the trap is caused by an external interrupt signal (async interrupt)
    insn_t null_insi(0);
    clear_curr_record();
    // to log this event, an artificial instruction is inserted
    // to distinguish it from the real, core fetched instruction, pc is set to all 1 and instruction is set to NULL
    m_rec_insn.pc = -1;
    m_rec_insn.insn = null_insi;
    m_rec_insn.good = false;
    m_rec_insn.valid = true;
    m_rec_insn.seqno = m_insn_seq;
    m_rec_insn.cycle = m_insn_seq;
    m_rec_insn.instret = m_instret;
    m_rec_insn.post_exe_state = *m_tgt_proc->get_state();
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

void debug_tracer_t::trace_before_dc_translate(reg_t vaddr, bool write) {
  if (!m_enabled)
    return;

  assert(m_rec_insn.valid && m_rec_insn.good);

  m_rec_insn.mem_rec.vaddr = vaddr;
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
    m_rec_insn.mem_rec.write == write
  );

  m_rec_insn.mem_rec.val = val;
  m_rec_insn.mem_rec.paddr = paddr;
  m_rec_insn.mem_rec.op_size = size;
}


void debug_tracer_t::drain_curr_record() {
  if (m_rec_insn.valid) {
    m_trace_output->issue_insn(m_rec_insn);
    seqno_incr();
  }
  clear_curr_record();
}


void debug_tracer_t::clear_curr_record() {
  memset(&m_rec_insn, 0, sizeof(m_rec_insn));
}

void debug_tracer_t::seqno_incr() {
  if (unlikely(m_insn_seq & ((1ul << 24ul) - 1ul)) == 0ul) {
    fprintf(stderr, "Traced 0x%" PRIX64 " instructions.\n", m_insn_seq);
  }
  ++m_insn_seq;
}

/************* Trace Output *************/
trace_output_direct_t::trace_output_direct_t(const std::string &filename_out) {
  m_trace_file_name = filename_out;
  m_tr_ostream.open(filename_out.c_str());
  if (!m_tr_ostream.good()) {
    std::cerr << "Trace output error: fail to open trace output file" << m_trace_file_name << std::endl;
    exit(1);
  }
}

trace_output_direct_t::~trace_output_direct_t() {
  std::cout << std::endl << "Saving trace \"" << m_trace_file_name << "\"..." << std::endl;
  m_tr_ostream.flush();
  m_tr_ostream.close();
}

#ifdef __DBG_TRACE_DEBUG_OUTPUT
#define ogzs_printf(ogzs, format, ...) \
  do { \
    char buf[1024]; \
    if (snprintf(buf, sizeof(buf), format, __VA_ARGS__) >= int64_t(sizeof(buf))) { \
      fprintf(stderr, "trace output error: formating buffer overflow\n"); \
      exit(-1); \
    } else { \
      ogzs << buf; \
      ogzs.flush(); \
    } \
  } while(0)
#else
#define ogzs_printf(ogzs, format, ...) \
  do { \
    char buf[1024]; \
    if (snprintf(buf, sizeof(buf), format, __VA_ARGS__) >= int64_t(sizeof(buf))) { \
      fprintf(stderr, "trace output error: formating buffer overflow\n"); \
      exit(-1); \
    } else { \
      ogzs << buf; \
    } \
  } while(0)
#endif
#define PRIcycle PRIu64

void trace_output_direct_t::output_insn_record(const insn_record_t &insn_rec) {
  if (insn_rec.valid) {
    auto insn = insn_rec.insn;
    auto insn_pc = insn_rec.pc;

    ogzs_printf(m_tr_ostream, "S/%" PRIcycle " C/%" PRIu64 " I/%" PRIu64 " PC/0x%016" PRIx64 " (0x%08" PRIx64 ") %s\n",
                insn_rec.seqno, insn_rec.cycle, insn_rec.instret, insn_pc, insn.bits() & 0xffffffff,
                m_disassembler.disassemble(insn).c_str());
    if (!insn_rec.good) {
      ogzs_printf(m_tr_ostream, "%s", "\tINV_FETCH\t0x00000001\n");
    }
    for (size_t rs_idx = 0; rs_idx < MAX_RSRC; ++rs_idx)
      if (insn_rec.rs_rec[rs_idx].valid)
        ogzs_printf(
          m_tr_ostream, "\tRS%" PRIu64 "/%s\t0x%08" PRIx64 "\n",
          rs_idx, xpr_name[insn_rec.rs_rec[rs_idx].n],
          insn_rec.rs_rec[rs_idx].val.xval
        );

    for (auto &rd : insn_rec.rd_rec)
      if (rd.valid && rd.n != 0)
        ogzs_printf(
          m_tr_ostream, "\tRD/%s\t0x%08" PRIx64 "\n",
          xpr_name[rd.n], rd.val.xval
        );

    if (insn.opcode() == OP_LOAD || insn.opcode() == OP_STORE) {
      assert(insn_rec.mem_rec.valid);
      ogzs_printf(m_tr_ostream, "\tADDR\t0x%08" PRIx64 "\n", insn_rec.mem_rec.vaddr);
    } else if (insn.opcode() == OP_BRANCH || insn.opcode() == OP_JAL || insn.opcode() == OP_JALR) {
      reg_t taken_target = 0;
      switch (insn.opcode()) {
        case OP_BRANCH:
          taken_target = insn_pc + insn.sb_imm();
          break;
        case OP_JAL:
          taken_target = insn_pc + insn.uj_imm();
          break;
        case OP_JALR:
          assert(insn_rec.rs_rec[0].valid);
          taken_target = ((insn_rec.rs_rec[0].val.xval + insn.i_imm()) & ~reg_t(1));
          break;
        default:
          assert(0);
      }
      ogzs_printf(m_tr_ostream, "\tTAKEN_PC\t0x%08" PRIx64 "\n", taken_target);
    }

    if (insn_rec.exception) {
      ogzs_printf(m_tr_ostream, "\tEXCEPTION\t0x%016" PRIx64 "\n", 1l);
      ogzs_printf(m_tr_ostream, "\tEVEC\t0x%016" PRIx64 "\n", insn_rec.post_exe_state.evec);
      ogzs_printf(m_tr_ostream, "\tECAUSE\t0x%016" PRIx64 "\n", insn_rec.post_exe_state.cause);
      ogzs_printf(m_tr_ostream, "\tEPC\t0x%016" PRIx64 "\n", insn_rec.post_exe_state.epc);
      ogzs_printf(m_tr_ostream, "\tSR\t0x%08" PRIx32 "\n", insn_rec.post_exe_state.sr);
    }
    ogzs_printf(m_tr_ostream, "%s", "\n");
  }
}

trace_output_last_n_t::trace_output_last_n_t(const std::string &filename_out, size_t n) :
  m_direct_output(filename_out) {
  m_sz_buf = n;
  fprintf(stderr, "*** Reserved %" PRId64 " MB memory for keeping the history of %" PRId64 " instructions ***\n",
          (sizeof(insn_record_t) * n) >> 20ul, n);
  m_insn_rec_circ_buf = new insn_record_t[n];
  m_tail = 0;
  m_head = 0;
  empty = true;
}

trace_output_last_n_t::~trace_output_last_n_t() {
  for (
    insn_record_t *p = insn_rec_circ_buf_pop();
    p != nullptr;
    p = insn_rec_circ_buf_pop()
    ) {
    m_direct_output.issue_insn(*p);
  }
  delete[] m_insn_rec_circ_buf;
}

void trace_output_last_n_t::issue_insn(const insn_record_t &insn) {
  insn_rec_circ_buf_push(insn);
}

void trace_output_last_n_t::insn_rec_circ_buf_push(const insn_record_t &insn_rec) {
  m_insn_rec_circ_buf[m_tail] = insn_rec;

  if (m_tail == m_head) {
    if (empty) {
      m_tail = next_idx(m_tail);
      empty = false;
    } else {
      m_head = m_tail = next_idx(m_tail);
    }
  } else {
    m_tail = next_idx(m_tail);
  }
}

insn_record_t *trace_output_last_n_t::insn_rec_circ_buf_pop() {
  insn_record_t *ret_ptr = nullptr;

  if (!empty) {
    ret_ptr = m_insn_rec_circ_buf + m_head;
    m_head = next_idx(m_head);
    if (m_head == m_tail)
      empty = true;
  }

  return ret_ptr;
}

#endif /* RISCV_ENABLE_DBG_TRACE */
