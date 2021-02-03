//
// Created by s117 on 1/22/21.
//

#ifndef RISCV_ISA_SIM_RISCV_RECONV_PREDICTOR_H
#define RISCV_ISA_SIM_RISCV_RECONV_PREDICTOR_H

#include "reconv_predictor.h"
#include "decode.h"

class riscv_reconv_predictor {
public:
  reconv_predictor m_reconv_predictor;

  // only training the reconvergence predictor when executing user mode code
  void on_userspace_insn_retired(insn_t retired_insn, uint64_t pc, uint64_t npc) {
    const auto retired_insn_opcode = retired_insn.opcode();
    const static int REG_RA = 1;
    const static int REG_ZERO = 0;

    if (retired_insn_opcode == OP_JAL && retired_insn.rd() == REG_RA) {
      // call
      m_reconv_predictor.on_function_call(pc, npc);
    } else if (retired_insn_opcode == OP_JALR && retired_insn.rd() == REG_RA) {
      // indirect call
      m_reconv_predictor.on_function_call(pc, npc);
    } else if (
      retired_insn_opcode == OP_JALR &&
      retired_insn.rs1() == REG_RA && retired_insn.rd() == REG_ZERO
      ) {
      // return
      m_reconv_predictor.on_function_return(pc, npc);
    } else if (retired_insn_opcode == OP_JALR) {
      // deem all the rest JALR as indirect jump
      m_reconv_predictor.on_indirect_jmp_retired(pc, npc);
    } else if (retired_insn_opcode == OP_BRANCH) {
      // branches' target usually is anything but pc + 4
      m_reconv_predictor.on_branch_retired(pc, npc, pc + 4 == npc);
    } else {
      // all others
      m_reconv_predictor.on_other_insn_retired(pc);
    }
  };
};

#endif //RISCV_ISA_SIM_RISCV_RECONV_PREDICTOR_H
