#ifndef _EXECUTE_INSN_HOOK_H
#define _EXECUTE_INSN_HOOK_H
#include "processor.h"
#include "bbtracker.h"
#include "pc_freqvec_tracker.h"

extern bool logging_on;

#ifdef RISCV_ENABLE_COMMITLOG
static void commit_log(state_t *state, reg_t pc, insn_t insn)
{
  if (state->sr & SR_EI)
  {
    uint64_t mask = (insn.length() == 8 ? uint64_t(0) : (uint64_t(1) << (insn.length() * 8))) - 1;
    if (state->log_reg_write.addr)
    {
      fprintf(stderr, "0x%016" PRIx64 " (0x%08" PRIx64 ") %c%2" PRIu64 " 0x%016" PRIx64 "\n",
              pc,
              insn.bits() & mask,
              state->log_reg_write.addr & 1 ? 'f' : 'x',
              state->log_reg_write.addr >> 1,
              state->log_reg_write.data);
    }
    else
    {
      fprintf(stderr, "0x%016" PRIx64 " (0x%08" PRIx64 ")\n", pc, insn.bits() & mask);
    }
  }
  state->log_reg_write.addr = 0;
}
#endif

void processor_t::pre_execute_insn(reg_t pc, insn_t insn)
{
#ifdef RISCV_ENABLE_DBG_TRACE
  get_dbg_tracer()->trace_before_insn_execute(pc, insn);
#endif

  htif_exec_ctrl.pre_execution_check(pc);
}

void processor_t::post_execute_insn(reg_t pc, insn_t insn, reg_t npc, bool trapped)
{
  htif_exec_ctrl.on_instret_increment();

#ifdef RISCV_ENABLE_DBG_TRACE
  dbg_tracer->increment_instret();
  if (!trapped) dbg_tracer->trace_after_insn_execute(pc);
#endif

#ifdef RISCV_ENABLE_SIMPOINT
  ++num_bb_inst;
  if (simpoint_enabled)
  {
    reg_t opcode = insn.opcode();
    if (opcode == OP_JAL || opcode == OP_JALR || opcode == OP_BRANCH)
    {
  #ifdef RISCV_ENABLE_PC_FREQ_VEC
      if (unlikely(bbt->bb_tracker((uint64_t) pc, num_bb_inst)))
        get_pc_freqvec_tracker()->finish_vec();
  #else
      bbt->bb_tracker((uint64_t) pc, num_bb_inst);
  #endif
      num_bb_inst = 0;
    }
  #ifdef RISCV_ENABLE_PC_FREQ_VEC
    pc_freqvec_tracker->update_vec(pc);
  #endif
  }
#endif

#ifdef RISCV_ENABLE_HISTOGRAM
  update_histogram(pc);
#endif

#ifdef RISCV_ENABLE_COMMITLOG
  commit_log(get_state(), pc, insn);
#endif
}

#endif //_EXECUTE_INSN_HOOK_H
