// See LICENSE for license details.
#ifndef _RISCV_PROCESSOR_H
#define _RISCV_PROCESSOR_H

#include "decode.h"
#include "config.h"
#include <cstring>
#include <vector>
#include <map>

class processor_t;
class mmu_t;
typedef reg_t (*insn_func_t)(processor_t*, insn_t, reg_t);
class sim_t;
class trap_t;
class extension_t;
class disassembler_t;
class bb_tracker_t;
class debug_tracer_t;
class pc_freqvec_tracker_t;

struct insn_desc_t
{
  uint32_t match;
  uint32_t mask;
  insn_func_t rv32;
  insn_func_t rv64;
};

struct commit_log_reg_t
{
  reg_t addr;
  reg_t data;
};

// architectural state of a RISC-V hart
struct state_t
{
  void reset();

  reg_t pc;
  regfile_t<reg_t, NXPR, true> XPR;
  regfile_t<freg_t, NFPR, false> FPR;

  // control and status registers
  reg_t epc;
  reg_t badvaddr;
  reg_t evec;
  reg_t ptbr;
  reg_t pcr_k0;
  reg_t pcr_k1;
  reg_t cause;
  reg_t tohost;
  reg_t fromhost;
  reg_t count;
  uint32_t compare;
  uint32_t sr; // only modify the status register using set_pcr()
  uint32_t fflags;
  uint32_t frm;

  reg_t load_reservation;

#ifdef RISCV_ENABLE_COMMITLOG
  commit_log_reg_t log_reg_write;
#endif
};

// this class represents one processor in a RISC-V machine.
class processor_t
{
public:
  processor_t(sim_t* _sim, mmu_t* _mmu, uint32_t _id);
  ~processor_t();

  void set_debug(bool value);
  void set_histogram(bool value);
  void reset(bool value);
  size_t step(size_t n); // run for n cycles
  void deliver_ipi(); // register an interprocessor interrupt
  bool running() { return run; }
  void set_pcr(int which, reg_t val);
  void set_fromhost(reg_t val);
  void set_interrupt(int which, bool on);
  reg_t get_pcr(int which);
  mmu_t* get_mmu() { return mmu; }
  state_t* get_state() { return &state; }
  extension_t* get_extension() { return ext; }
  uint32_t get_id() { return id; }
  void yield_load_reservation() { state.load_reservation = (reg_t)-1; }
  void update_histogram(size_t pc);

  void register_insn(insn_desc_t);
  void register_extension(extension_t*);
#ifdef RISCV_ENABLE_SIMPOINT
  bool simpoint_enabled;
  uint64_t num_bb_inst;
  bb_tracker_t* get_bbt() { return bbt; }
  pc_freqvec_tracker_t* get_pc_freqvec_tracker() { return pc_freqvec_tracker; }
  void set_simpoint(bool enable, size_t interval);

  bool get_simpoint() { return simpoint_enabled; };
#endif

#ifdef RISCV_ENABLE_DBG_TRACE
  void enable_trace(size_t n);
  void enable_insn_info_collection();
  debug_tracer_t* get_dbg_tracer() { return dbg_tracer; };
  reg_t rd_xpr(size_t rn, operand_t operand);
  void wr_xpr(size_t rn, reg_t val);
  freg_t rd_fpr(size_t rn, operand_t operand);
  void wr_fpr(size_t rn, freg_t val);
#endif

private:
  sim_t* sim;
  mmu_t* mmu; // main memory is always accessed via the mmu
  extension_t* ext;
  disassembler_t* disassembler;

#ifdef RISCV_ENABLE_SIMPOINT
  bb_tracker_t* bbt;
  pc_freqvec_tracker_t* pc_freqvec_tracker;
#endif

#ifdef RISCV_ENABLE_DBG_TRACE
  debug_tracer_t* dbg_tracer;
#endif

  state_t state;
  uint32_t id;
  bool run; // !reset
  bool debug;
  bool histogram_enabled;
  bool rv64;
  bool serialized;

  std::vector<insn_desc_t> instructions;
  std::vector<insn_desc_t*> opcode_map;
  std::vector<insn_desc_t> opcode_store;
  std::map<size_t,size_t> pc_histogram;

  void take_interrupt(); // take a trap if any interrupts are pending
  void serialize(); // collapse into defined architectural state
  reg_t take_trap(trap_t& t, reg_t epc); // take an exception
  void disasm(insn_t insn); // disassemble and print an instruction
  void disasm(insn_t insn,reg_t pc); // disassemble and print an instruction

  friend class sim_t;
  friend class mmu_t;
  friend class extension_t;

  void build_opcode_map();
  insn_func_t decode_insn(insn_t insn);
};

reg_t illegal_instruction(processor_t* p, insn_t insn, reg_t pc);

#define REGISTER_INSN(proc, name, match, mask) \
  extern reg_t rv32_##name(processor_t*, insn_t, reg_t); \
  extern reg_t rv64_##name(processor_t*, insn_t, reg_t); \
  proc->register_insn((insn_desc_t){match, mask, rv32_##name, rv64_##name});

#endif
