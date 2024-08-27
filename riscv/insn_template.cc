// See LICENSE for license details.

#include "insn_template.h"
#include "execute_insn_hook.h"

reg_t rv32_NAME(processor_t* p, insn_t insn, reg_t pc)
{
  p->pre_execute_insn(pc, insn);
  int xlen = 32;
  reg_t npc = sext_xlen(pc + insn_length(OPCODE));
  #include "insns/NAME.h"
  p->post_execute_insn(pc, insn, npc, false);
  return npc;
}

reg_t rv64_NAME(processor_t* p, insn_t insn, reg_t pc)
{
  p->pre_execute_insn(pc, insn);
  int xlen = 64;
  reg_t npc = sext_xlen(pc + insn_length(OPCODE));
  #include "insns/NAME.h"
  p->post_execute_insn(pc, insn, npc, false);
  return npc;
}
