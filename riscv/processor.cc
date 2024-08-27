// See LICENSE for license details.

#include "bbtracker.h"
#include "pc_freqvec_tracker.h"
#include "processor.h"
#include "extension.h"
#include "common.h"
#include "config.h"
#include "sim.h"
#include "htif.h"
#include "disasm.h"
#include "debug_tracer.h"
#include <cinttypes>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <assert.h>
#include <limits.h>
#include <stdexcept>
#include <algorithm>
#include "unistd.h"
#include <limits.h>
#include "execute_insn_hook.h"

#undef STATE
#define STATE state

extern bool logging_on;

processor_t::processor_t(sim_t* _sim, mmu_t* _mmu, uint32_t _id)
  : sim(_sim), mmu(_mmu), ext(NULL), disassembler(new disassembler_t),
    id(_id), run(false), debug(false), serialized(false), waiting_host(false)
{
#ifdef RISCV_ENABLE_DBG_TRACE
  dbg_tracer = new debug_tracer_t(this);
#endif

  reset(true);
  mmu->set_processor(this);

  #define DECLARE_INSN(name, match, mask) REGISTER_INSN(this, name, match, mask)
  #include "encoding.h"
  #undef DECLARE_INSN
  build_opcode_map();

#ifdef RISCV_ENABLE_SIMPOINT
  num_bb_inst = 0;
  simpoint_enabled = false;
  bbt = new bb_tracker_t();
#ifdef RISCV_ENABLE_PC_FREQ_VEC
  pc_freqvec_tracker = new pc_freqvec_tracker_t();
#endif
#endif
}

processor_t::~processor_t()
{
#ifdef RISCV_ENABLE_HISTOGRAM
  if (histogram_enabled)
  {
    fprintf(stderr, "PC Histogram size:%lu\n", pc_histogram.size());
    for(auto iterator = pc_histogram.begin(); iterator != pc_histogram.end(); ++iterator) {
      fprintf(stderr, "%0lx %lu\n", (iterator->first << 2), iterator->second);
    }
  }
#endif

#ifdef RISCV_ENABLE_SIMPOINT
  delete bbt;
#ifdef RISCV_ENABLE_PC_FREQ_VEC
  delete pc_freqvec_tracker;
#endif
#endif

#ifdef RISCV_ENABLE_DBG_TRACE
  delete dbg_tracer;
#endif

  delete disassembler;
}

void state_t::reset()
{
  // the ISA guarantees on boot that the PC is 0x2000 and the the processor
  // is in supervisor mode, and in 64-bit mode, if supported, with traps
  // and virtual memory disabled.
  sr = SR_S | SR_S64 | SR_U64;
  pc = 0x2000;

  // the following state is undefined upon boot-up,
  // but we zero it for determinism
  XPR.reset();
  FPR.reset();

  epc = 0;
  badvaddr = 0;
  evec = 0;
  ptbr = 0;
  pcr_k0 = 0;
  pcr_k1 = 0;
  cause = 0;
  tohost = 0;
  fromhost = 0;
  count = 0;
  compare = 0;
  fflags = 0;
  frm = 0;

  load_reservation = -1;
}

void processor_t::set_debug(bool value)
{
  debug = value;
  if (ext)
    ext->set_debug(value);
}

void processor_t::set_histogram(bool value)
{
  histogram_enabled = value;
}

#ifdef RISCV_ENABLE_SIMPOINT
void processor_t::set_simpoint(bool enable, size_t interval)
{
  simpoint_enabled = enable;

  if (enable) {
    std::string bbv_file = std::string("bbv_proc_") + std::to_string(id);
    std::string pcfvec_file = std::string("pcfvec_proc_") + std::to_string(id);
#if defined(__APPLE__) && defined(__MACH__)
    char curr_dir[PATH_MAX];
    getcwd(curr_dir, PATH_MAX);
#else
    char* curr_dir = get_current_dir_name();
#endif
    bbt->init_bb_tracker(curr_dir, bbv_file.c_str());
    bbt->set_interval_size(interval);

#ifdef RISCV_ENABLE_PC_FREQ_VEC
    pc_freqvec_tracker->init_pc_freqvec_tracker(curr_dir, pcfvec_file.c_str());
#endif

#if !(defined(__APPLE__) && defined(__MACH__))
    free(curr_dir);
#endif
  }
}

#endif

#ifdef RISCV_ENABLE_DBG_TRACE
void processor_t::enable_trace(size_t n)
{
  if (!dbg_tracer->enabled()) {
    #ifdef __DBG_TRACE_DEBUG_OUTPUT
    std::string trace_file_name = std::string("trace_proc_") + std::to_string(get_id()) + ".txt";
    #else
    std::string trace_file_name = std::string("trace_proc_") + std::to_string(get_id()) + ".gz";
    #endif
    trace_output_t *trace_outputter;
    if (n != 0) {
      trace_outputter = new trace_output_last_n_t(trace_file_name, n);
    } else {
      trace_outputter = new trace_output_direct_t(trace_file_name);
    }
    dbg_tracer->enable_trace(trace_outputter);
  }
}

void processor_t::enable_insn_info_collection() {
  if (!dbg_tracer->enabled()) {
    dbg_tracer->enable_trace(new trace_output_null_t());
  }
}

reg_t processor_t::rd_xpr(size_t rn, operand_t operand)
{
  auto val = state.XPR[rn];
  dbg_tracer->trace_after_xpr_access(rn, val, operand);
  return val;
}

void processor_t::wr_xpr(size_t rn, reg_t val)
{
  state.XPR.write(rn, val);
  dbg_tracer->trace_after_xpr_access(rn, val, RDST_OPERAND);
}

freg_t processor_t::rd_fpr(size_t rn, operand_t operand)
{
  auto val = state.FPR[rn];
  dbg_tracer->trace_after_fpr_access(rn, val, operand);
  return val;
}

void processor_t::wr_fpr(size_t rn, freg_t val)
{
  state.FPR.write(rn, val);
  dbg_tracer->trace_after_fpr_access(rn, val, RDST_OPERAND);
}
#endif

void processor_t::reset(bool value)
{

  fprintf(stderr, "******* Resetting core ********** \n");
  if (run == !value)
    return;
  run = !value;

  state.reset(); // reset the core
  set_pcr(CSR_STATUS, state.sr);

  if (ext)
    ext->reset(); // reset the extension
}

struct serialize_t {};

void processor_t::serialize()
{
  if (serialized)
    serialized = false;
  else
    serialized = true, throw serialize_t();
}

void processor_t::take_interrupt()
{
  int irqs = ((state.sr & SR_IP) >> SR_IP_SHIFT) & (state.sr >> SR_IM_SHIFT);
  if (likely(!irqs) || likely(!(state.sr & SR_EI)))
    return;

  for (int i = 0; ; i++)
    if ((irqs >> i) & 1)
      throw trap_t((1ULL << ((state.sr & SR_S64) ? 63 : 31)) + i);
}

inline reg_t processor_t::execute_insn(const reg_t pc, const insn_fetch_t fetch)
{
  reg_t npc = fetch.func(this, fetch.insn, pc);
  return npc;
}

static void update_timer(state_t* state, size_t instret)
{
  uint64_t count0 = (uint64_t)(uint32_t)state->count;
  state->count += instret;
  uint64_t before = count0 - state->compare;
  if (int64_t(before ^ (before + instret)) < 0)
    state->sr |= (1 << (IRQ_TIMER + SR_IP_SHIFT));
}

static size_t next_timer(state_t* state)
{
  return state->compare - (uint32_t)state->count;
}


/**
 * Step for either 0 or exact n instructions.
 *
 * @param n Number of instruction to step
 * @return size_t Number of instructions stepped.
 */
size_t processor_t::step(const size_t n)
{
  if (unlikely(!run))
    return 0;

  size_t instret = 0;

  // Simulating instructions in batch.
  while (instret < n)
  {
    // Maximum allowed instret after the batch.
    size_t batch_instret_max = std::min(n, instret + (next_timer(&state) | 1U));

    // Instruction budget for the current batch.
    auto batch_instret_budget = batch_instret_max - instret;

    reg_t pc = state.pc;
    try
    {
      take_interrupt();

      if (unlikely(debug))
      {
        while (batch_instret_budget > 0)
        {
          insn_fetch_t fetch = mmu->load_insn(pc);
          disasm(fetch.insn);
          pc = execute_insn(pc, fetch);
          --batch_instret_budget;
          fprintf(stderr, "RS1: %" PRIu64 " RS2: %" PRIu64 " RD: %" PRIu64 "\n", STATE.XPR[fetch.insn.rs1()], STATE.XPR[fetch.insn.rs2()], STATE.XPR[fetch.insn.rd()]);
        }
      }
      else
      {
        while (batch_instret_budget > 0)
        {
          const size_t idx = mmu->icache_index(pc);
          auto ic_entry = mmu->access_icache(pc);

#ifdef STEPPING_WITHOUT_DUFF_DEVICE
          do {
            pc = execute_insn(pc, ic_entry->data);
            ic_entry++;
            --batch_instret_budget;
          } while ((batch_instret_budget) && (ic_entry->tag == pc));
#else
          switch (idx)
          {
            #define ICACHE_ACCESS(idx)                                                     \
              {                                                                            \
                pc = execute_insn(pc, ic_entry->data);                                     \
                ic_entry++;                                                                \
                --batch_instret_budget;                                                    \
                if (unlikely((batch_instret_budget == 0) || (ic_entry->tag != pc))) break; \
              }
            #include "icache.h"
            break;
          }
#endif
        }
      }

      // Update state
      auto new_instret = batch_instret_max - batch_instret_budget;
      update_timer(&state, new_instret - instret);
      state.pc = pc;
      instret = new_instret;
    }
    catch (trap_t &t)
    {
      // Take the trap - Get PC on handler
      auto trap_handler = take_trap(t, pc);

      // count scall and sbreak instructions
      if (dynamic_cast<trap_syscall *>(&t) || dynamic_cast<trap_breakpoint *>(&t))
      {
        --batch_instret_budget;
        post_execute_insn(pc, mmu->access_icache(pc)->data.insn, trap_handler, true);
      }

      // Update state
      pc = trap_handler;
      auto new_instret = batch_instret_max - batch_instret_budget;
      update_timer(&state, new_instret - instret);
      state.pc = pc;
      instret = new_instret;
    }
    catch (serialize_t &s)
    {
      // Encountered an instruction accessing the precise HART state, therefore needs to be serialized.
      // The instruction will be re-fetched and re-executed, so don't count it at the moment.

      // Update state
      auto new_instret = batch_instret_max - batch_instret_budget;
      update_timer(&state, new_instret - instret);
      state.pc = pc;
      instret = new_instret;
    }
    catch (core_frozen_t &f)
    {
      // The core was frozen by the execution controller.
      // Update state before ticking HTIF, so that the host will see a precise target state.
      auto new_instret = batch_instret_max - batch_instret_budget;
      update_timer(&state, new_instret - instret);
      state.pc = pc;
      instret = new_instret;
      // Keep ticking HTIF to get the core defrost sooner.
      while (htif_exec_ctrl.frozen_state()) sim->get_htif()->tick();
    }
  }

  assert(n == instret);
  return instret;
}

reg_t processor_t::take_trap(trap_t& t, reg_t epc)
{
  //TODO: Add this back
  //if (debug)
  ifprintf(logging_on,stderr, "core %3d: exception %s, epc 0x%016" PRIx64 "\n",
           id, t.name(), epc);

  // switch to supervisor, set previous supervisor bit, disable interrupts
  set_pcr(CSR_STATUS, (((state.sr & ~SR_EI) | SR_S) & ~SR_PS & ~SR_PEI) |
                      ((state.sr & SR_S) ? SR_PS : 0) |
                      ((state.sr & SR_EI) ? SR_PEI : 0));

  yield_load_reservation();
  state.cause = t.cause();
  state.epc = epc;
  t.side_effects(&state); // might set badvaddr etc.

#ifdef RISCV_ENABLE_DBG_TRACE
  dbg_tracer->trace_after_take_trap(t, state.epc, state.evec);
#endif

#if defined(RISCV_ENABLE_SIMPOINT) && defined(RISCV_ENABLE_PC_FREQ_VEC)
  pc_freqvec_tracker->update_vec(state.evec);
#endif

  return state.evec;
}

void processor_t::deliver_ipi()
{
  if (run)
    set_pcr(CSR_CLEAR_IPI, 1);
}

void processor_t::disasm(insn_t insn)
{
  uint64_t bits = insn.bits() & ((1ULL << (8 * insn_length(insn.bits()))) - 1);
  fprintf(stderr, "core %3d: 0x%016" PRIx64 " (0x%08" PRIx64 ") %s\n",
          id, state.pc, bits, disassembler->disassemble(insn).c_str());
}

void processor_t::disasm(insn_t insn,reg_t pc)
{
  uint64_t bits = insn.bits() & ((1ULL << (8 * insn_length(insn.bits()))) - 1);
  fprintf(stderr, "core %3d: 0x%016" PRIx64 " (0x%08" PRIx64 ") %s\n",
          id, pc, bits, disassembler->disassemble(insn).c_str());
}

void processor_t::set_pcr(int which, reg_t val)
{
  switch (which)
  {
    case CSR_FFLAGS:
      state.fflags = val & (FSR_AEXC >> FSR_AEXC_SHIFT);
      break;
    case CSR_FRM:
      state.frm = val & (FSR_RD >> FSR_RD_SHIFT);
      break;
    case CSR_FCSR:
      state.fflags = (val & FSR_AEXC) >> FSR_AEXC_SHIFT;
      state.frm = (val & FSR_RD) >> FSR_RD_SHIFT;
      break;
    case CSR_STATUS:
      state.sr = (val & ~SR_IP) | (state.sr & SR_IP);
#ifndef RISCV_ENABLE_64BIT
      state.sr &= ~(SR_S64 | SR_U64);
#endif
#ifndef RISCV_ENABLE_FPU
      state.sr &= ~SR_EF;
#endif
      if (!ext)
        state.sr &= ~SR_EA;
      state.sr &= ~SR_ZERO;
      rv64 = (state.sr & SR_S) ? (state.sr & SR_S64) : (state.sr & SR_U64);
      mmu->flush_tlb();
      break;
    case CSR_EPC:
      state.epc = val;
      break;
    case CSR_EVEC:
      state.evec = val & ~3;
      break;
    case CSR_COUNT:
      state.count = val;
      break;
    case CSR_COUNTH:
      state.count = (val << 32) | (uint32_t)state.count;
      break;
    case CSR_COMPARE:
      serialize();
      set_interrupt(IRQ_TIMER, false);
      state.compare = val;
      break;
    case CSR_PTBR:
      state.ptbr = val & ~(PGSIZE-1);
      break;
    case CSR_SEND_IPI:
      sim->send_ipi(val);
      break;
    case CSR_CLEAR_IPI:
      set_interrupt(IRQ_IPI, val & 1);
      break;
    case CSR_SUP0:
      state.pcr_k0 = val;
      break;
    case CSR_SUP1:
      state.pcr_k1 = val;
      break;
    case CSR_TOHOST:
      if (state.tohost == 0)
      {
        #define GET_COMMAND_DEV_ID(tohost_val) ((uint8_t)((tohost_val) >> 56))
        #define DEV_ID_SYSCALL ((uint8_t)0)

        state.tohost = val;
        if (GET_COMMAND_DEV_ID(val) == DEV_ID_SYSCALL)
        {
          waiting_host = true;
          while (waiting_host) sim->get_htif()->tick();
        }
      }
      break;
    case CSR_FROMHOST:
      set_fromhost(val);
      break;
  }
}

void processor_t::set_fromhost(reg_t val)
{
  set_interrupt(IRQ_HOST, val != 0);
  state.fromhost = val;
  ifprintf(logging_on,stderr,"setting FROMHOST to %lu  STATUS = %u\n",val,state.sr);
  if (val) waiting_host = false;
}

reg_t processor_t::get_pcr(int which)
{
  switch (which)
  {
    case CSR_FFLAGS:
      require_fp;
      return state.fflags;
    case CSR_FRM:
      require_fp;
      return state.frm;
    case CSR_FCSR:
      require_fp;
      return (state.fflags << FSR_AEXC_SHIFT) | (state.frm << FSR_RD_SHIFT);
    case CSR_STATUS:
      return state.sr;
    case CSR_EPC:
      return state.epc;
    case CSR_BADVADDR:
      return state.badvaddr;
    case CSR_EVEC:
      return state.evec;
    case CSR_CYCLE:
    case CSR_TIME:
    case CSR_INSTRET:
    case CSR_COUNT:
      serialize();
      return state.count;
    case CSR_CYCLEH:
    case CSR_TIMEH:
    case CSR_INSTRETH:
    case CSR_COUNTH:
      if (rv64)
        break;
      serialize();
      return state.count >> 32;
    case CSR_COMPARE:
      return state.compare;
    case CSR_CAUSE:
      return state.cause;
    case CSR_PTBR:
      return state.ptbr;
    case CSR_SEND_IPI:
    case CSR_CLEAR_IPI:
      return 0;
    case CSR_ASID:
      return 0;
    case CSR_FATC:
      mmu->flush_tlb();
      return 0;
    case CSR_HARTID:
      return id;
    case CSR_IMPL:
      return 1;
    case CSR_SUP0:
      return state.pcr_k0;
    case CSR_SUP1:
      return state.pcr_k1;
    case CSR_TOHOST:
      sim->get_htif()->tick(); // not necessary, but faster
      return state.tohost;
    case CSR_FROMHOST:
      sim->get_htif()->tick(); // not necessary, but faster
      return state.fromhost;
    case CSR_UARCH0:
    case CSR_UARCH1:
    case CSR_UARCH2:
    case CSR_UARCH3:
    case CSR_UARCH4:
    case CSR_UARCH5:
    case CSR_UARCH6:
    case CSR_UARCH7:
    case CSR_UARCH8:
    case CSR_UARCH9:
    case CSR_UARCH10:
    case CSR_UARCH11:
    case CSR_UARCH12:
    case CSR_UARCH13:
    case CSR_UARCH14:
    case CSR_UARCH15:
      return 0;
  }
  throw trap_illegal_instruction();
}

void processor_t::set_interrupt(int which, bool on)
{
  uint32_t mask = (1 << (which + SR_IP_SHIFT)) & SR_IP;
  if (on)
    state.sr |= mask;
  else
    state.sr &= ~mask;
}

reg_t illegal_instruction(processor_t* p, insn_t insn, reg_t pc)
{
  throw trap_illegal_instruction();
}

insn_func_t processor_t::decode_insn(insn_t insn)
{
  size_t mask = opcode_map.size()-1;
  insn_desc_t* desc = opcode_map[insn.bits() & mask];

  while ((insn.bits() & desc->mask) != desc->match)
    desc++;

  return rv64 ? desc->rv64 : desc->rv32;
}

void processor_t::register_insn(insn_desc_t desc)
{
  assert(desc.mask & 1);
  instructions.push_back(desc);
}

void processor_t::build_opcode_map()
{
  size_t buckets = -1;
  for (auto& inst : instructions)
    while ((inst.mask & buckets) != buckets)
      buckets /= 2;
  buckets++;

  struct cmp {
    decltype(insn_desc_t::match) mask;
    cmp(decltype(mask) mask) : mask(mask) {}
    bool operator()(const insn_desc_t& lhs, const insn_desc_t& rhs) {
      if ((lhs.match & mask) != (rhs.match & mask))
        return (lhs.match & mask) < (rhs.match & mask);
      return lhs.match < rhs.match;
    }
  };
  std::sort(instructions.begin(), instructions.end(), cmp(buckets-1));

  opcode_map.resize(buckets);
  opcode_store.resize(instructions.size() + 1);

  size_t j = 0;
  for (size_t b = 0, i = 0; b < buckets; b++)
  {
    opcode_map[b] = &opcode_store[j];
    while (i < instructions.size() && b == (instructions[i].match & (buckets-1)))
      opcode_store[j++] = instructions[i++];
  }

  assert(j == opcode_store.size()-1);
  opcode_store[j].match = opcode_store[j].mask = 0;
  opcode_store[j].rv32 = &illegal_instruction;
  opcode_store[j].rv64 = &illegal_instruction;
}

void processor_t::register_extension(extension_t* x)
{
  for (auto insn : x->get_instructions())
    register_insn(insn);
  build_opcode_map();
  for (auto disasm_insn : x->get_disasms())
    disassembler->add_insn(disasm_insn);
  if (ext != NULL)
    throw std::logic_error("only one extension may be registered");
  ext = x;
  x->set_processor(this);
}
