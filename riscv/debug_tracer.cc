#include "config.h"

#ifdef RISCV_ENABLE_DBG_TRACE

#ifdef RISCV_ENABLE_COMMITLOG
#error Configure flag --enable-commitlog is conflict with --enable-dbg-trace
#endif

#include <cstring>
#include <cassert>
#include <cinttypes>
#include <csignal>
#include "debug_tracer.h"
#include "mmu.h"

#define PRIcycle PRIu64

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

debug_tracer_t::debug_tracer_t(processor_t *target_processor) : m_disassembler(), m_rec_insn() {
  memset(&m_rec_insn, 0, sizeof(m_rec_insn));
  m_tgt_proc = target_processor;
  m_enabled = false;
  m_insn_seq = 0;

#ifdef __DBG_TRACE_DEBUG_OUTPUT
  m_trace_file_name = std::string("trace_proc_") + std::to_string(m_tgt_proc->get_id()) + ".txt";
#else
  m_trace_file_name = std::string("trace_proc_") + std::to_string(m_tgt_proc->get_id()) + ".gz";
#endif
}

debug_tracer_t::~debug_tracer_t() {
  if (m_enabled) {
    std::cout << std::endl << "Saving trace \"" << m_trace_file_name << "\"..." << std::endl;
    issue_curr_record();
    m_tr_ostream.flush();
    m_tr_ostream.close();
  }
}

void debug_tracer_t::enable_trace() {
  m_tr_ostream.open(m_trace_file_name.c_str());
  if (!m_tr_ostream.good()) {
    std::cerr << "Trace output error: fail to open trace output file" << m_trace_file_name << std::endl;
    exit(1);
  }
  m_enabled = true;
}

void debug_tracer_t::trace_before_insn_ic_fetch(reg_t pc) {
  if (!m_enabled)
    return;

  m_rec_insn.pc = pc;
  m_rec_insn.seqno = next_seqno();
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

  issue_curr_record();
  clear_curr_record();
}

void debug_tracer_t::trace_after_take_trap(trap_t &t, reg_t epc, reg_t new_pc) {
  if (m_rec_insn.valid) {
    // the trap is caused by an instruction (sync exception)
    assert(m_rec_insn.pc == epc);
    m_rec_insn.exception = true;
    issue_curr_record();
    clear_curr_record();
  } else {
    // the trap is caused by an external interrupt signal (async interrupt)
    clear_curr_record();
    // make an artificial nop instruction
    insn_t nop(0);

    m_rec_insn.pc = 0;
    m_rec_insn.insn = nop;
    m_rec_insn.exception = true;
    m_rec_insn.good = false;
    m_rec_insn.valid = true;
    m_rec_insn.seqno = next_seqno();
    issue_curr_record();
    clear_curr_record();
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
      assert(!m_rec_insn.rs_rec[operand].valid);
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
      assert(!m_rec_insn.rs_rec[operand].valid);
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

void debug_tracer_t::trace_after_dc_access(reg_t vaddr, freg_t val, size_t size, bool write) {
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
  m_rec_insn.mem_rec.op_size = size;
}


void debug_tracer_t::issue_curr_record() {
  if (m_rec_insn.valid) {
    auto insn = m_rec_insn.insn;
    auto insn_pc = m_rec_insn.pc;

    ogzs_printf(m_tr_ostream, "C/%" PRIcycle " S/%" PRIu64 " PC/0x%016" PRIx64 " (0x%08" PRIx64 ") %s\n",
                m_rec_insn.seqno, m_rec_insn.seqno, insn_pc, insn.bits() & 0xffffffff,
                m_disassembler.disassemble(insn).c_str());
    if (!m_rec_insn.good) {
      ogzs_printf(m_tr_ostream, "%s", "\tINV_FETCH\t0x00000001\n");
    }
    for (size_t rs_idx = 0; rs_idx < MAX_RSRC; ++rs_idx)
      if (m_rec_insn.rs_rec[rs_idx].valid)
        ogzs_printf(
          m_tr_ostream, "\tRS%" PRIu64 "/%s\t0x%08" PRIx64 "\n",
          rs_idx, xpr_name[m_rec_insn.rs_rec[rs_idx].n],
          m_rec_insn.rs_rec[rs_idx].val.xval
        );

    for (auto &rd_idx : m_rec_insn.rd_rec)
      if (rd_idx.valid && rd_idx.n != 0)
        ogzs_printf(
          m_tr_ostream, "\tRD/%s\t0x%08" PRIx64 "\n",
          xpr_name[rd_idx.n], rd_idx.val.xval
        );

    if (insn.opcode() == OP_LOAD || insn.opcode() == OP_STORE) {
      assert(m_rec_insn.mem_rec.valid);
      ogzs_printf(m_tr_ostream, "\tADDR\t0x%08" PRIx64 "\n", m_rec_insn.mem_rec.vaddr);
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
          assert(m_rec_insn.rs_rec[0].valid);
          taken_target = ((m_rec_insn.rs_rec[0].val.xval + insn.i_imm()) & ~reg_t(1));
          break;
        default:
          assert(0);
      }
      ogzs_printf(m_tr_ostream, "\tTAKEN_PC\t0x%08" PRIx64 "\n", taken_target);
    }

    if (m_rec_insn.exception) {
      auto proc_state = m_tgt_proc->get_state();
      ogzs_printf(m_tr_ostream, "\tEXCEPTION\t0x%016" PRIx64 "\n", 1l);
      ogzs_printf(m_tr_ostream, "\tEVEC\t0x%016" PRIx64 "\n", proc_state->evec);
      ogzs_printf(m_tr_ostream, "\tECAUSE\t0x%016" PRIx64 "\n", proc_state->cause);
      ogzs_printf(m_tr_ostream, "\tEPC\t0x%016" PRIx64 "\n", proc_state->epc);
      ogzs_printf(m_tr_ostream, "\tSR\t0x%08" PRIx32 "\n", proc_state->sr);
    }
    ogzs_printf(m_tr_ostream, "%s", "\n");
  }
}


void debug_tracer_t::clear_curr_record() {
  memset(&m_rec_insn, 0, sizeof(m_rec_insn));
}

uint64_t debug_tracer_t::next_seqno() {
  return ++m_insn_seq;
}


dbg_tracer_hook_mmu_t::dbg_tracer_hook_mmu_t(processor_t *upstream_processor, mmu_t *downstream_mmu) :
  mmu_t(nullptr, 0) {
  m_downstream_mmu = downstream_mmu;
  m_upstream_processor = upstream_processor;
  m_tracer_to_report = m_upstream_processor->get_dbg_tracer();
}

#endif
