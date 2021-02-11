//
// Created by s117 on 1/22/21.
//

#ifndef RISCV_ISA_SIM_RISCV_RECONV_PREDICTOR_H
#define RISCV_ISA_SIM_RISCV_RECONV_PREDICTOR_H

#include "reconv_predictor.h"
#include "decode.h"

#define RPT_DUMP_FILENAME "RPT_Result.csv"
#define RPT_NO_UNCOMMON_DUMP_FILENAME "RPT_Result_IgnoreUncommonPath.csv"
#define BFT_DUMP_FILENAME "BFT_Result.csv"

class riscv_reconv_predictor {
public:

  dynamic_reconv_predictor m_reconv_predictor;
  bool m_training_rpt = false;
  bool m_use_static_bft = false;

  bool m_dump_rpt;
  bool m_dump_bft;
  std::string m_rpt_dump_name;
  std::string m_bft_dump_name;

  static bool file_exist(const std::string &filepath) {
    return (access(filepath.c_str(), F_OK) != -1);
  }

  riscv_reconv_predictor() {
    if (file_exist(RPT_NO_UNCOMMON_DUMP_FILENAME)) {
      m_training_rpt = false;
      m_dump_rpt = false;
      m_dump_bft = false;
    } else {
      m_training_rpt = true;
      if (file_exist(BFT_DUMP_FILENAME)) {
        m_use_static_bft = true;
        m_dump_bft = false;
        m_dump_rpt = true;
        m_rpt_dump_name = RPT_NO_UNCOMMON_DUMP_FILENAME;
      } else {
        m_use_static_bft = false;
        m_dump_bft = true;
        m_bft_dump_name = BFT_DUMP_FILENAME;
        m_dump_rpt = true;
        m_rpt_dump_name = RPT_DUMP_FILENAME;
      }

    }

    if (m_training_rpt) {
      if (m_use_static_bft) {
        if (!m_reconv_predictor.m_BFT.load_static_stat_from_file(BFT_DUMP_FILENAME)) {
          std::cerr << "[RECONV PRED] (BFT) Fatal: fail to load BFT with static stats CSV [" << BFT_DUMP_FILENAME << "]"
                    << std::endl;
          exit(-1);
        }
        std::cerr << "[RECONV PRED] (BFT) BFT will be working in static mode using stats from file ["
                  << BFT_DUMP_FILENAME
                  << "]" << std::endl;
      } else {
        std::cerr << "[RECONV PRED] (BFT) BFT will be working in dynamic mode, and dump to [" << m_bft_dump_name << "]"
                  << std::endl;
        assert(m_dump_bft);
      }
      std::cerr << "[RECONV PRED] (RPT) RPT will be trained, and dump to [" << m_rpt_dump_name << "]" << std::endl;
      assert(m_dump_rpt);
    } else {
      std::cerr << "[RECONV PRED] (RPT) RPT will not be trained, since a static stats ["
                << RPT_NO_UNCOMMON_DUMP_FILENAME
                << "] exist." << std::endl;
    }
  }

  // only training the reconvergence predictor when executing user mode code
  void on_userspace_insn_retired(insn_t retired_insn, uint64_t pc, uint64_t npc) {
    if (!m_training_rpt)
      return;

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

  void dump_result() {
    if (m_dump_bft) {
      std::ofstream bft_csv(m_bft_dump_name);
      bft_csv << m_reconv_predictor.dump_BFT_result_csv();
      bft_csv.close();
    }
    if (m_dump_rpt) {
      std::ofstream rpt_csv(m_rpt_dump_name);
      rpt_csv << m_reconv_predictor.dump_RPT_result_csv();
      rpt_csv.close();
    }
  }
};

#undef RPT_DUMP_FILENAME
#undef BFT_DUMP_FILENAME
#endif //RISCV_ISA_SIM_RISCV_RECONV_PREDICTOR_H
