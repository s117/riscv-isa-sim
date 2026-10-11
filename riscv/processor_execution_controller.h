//
// Created by john on 7/28/24.
//

#ifndef _PROCESSOR_EXECUTION_CONTROLLER
#define _PROCESSOR_EXECUTION_CONTROLLER

#include "fesvr/hart_execution_reg.h"
#include "decode.h"
#include <cassert>
#include <cstdint>
#include <stdexcept>

// Thrown when the HART freezes in the middle of a batch, before an instruction executes: it ends the batch, and the HART
// waits to be defrosted at the next batch boundary, where its state is precise
class core_frozen_t : public std::exception
{
};

// Thrown when the HART is blocked waiting on the host, but the host has stopped the simulation (HTIF tick() returns false)
class host_stopped_t : public std::exception
{
};

/**
 * The HART side of the HTIF execution control (see fesvr/hart_execution_reg.h).
 *
 * Nothing here runs per instruction:
 *  - The unconditional and count-down triggers are evaluated at batch boundaries (at_batch_boundary()). The count down
 *    is kept as the retired-instruction count at which it runs out, and the stepping loop ends its batch right there.
 *  - A PC breakpoint is evaluated when the instruction at its PC is decoded into the icache (is_pc_break()): the
 *    icache entry then runs a stub that freezes the HART instead of the instruction.
 *  - A write from the host sets needs_attention(), so that the stepping loop ends its batch at the next instruction
 *    boundary and re-evaluates the triggers. The processor also flushes its icache on such a write, which installs or
 *    drops the PC breakpoint stubs, and makes the stepping loop leave its current run of cached instructions.
 */
class processor_execution_controller_t
{
private:
  constexpr static size_t NUM_PC_BREAK = 4;
  constexpr static uint8_t PC_BREAK_MASK_ALL = EXE_CTRL_MASK_PC0 | EXE_CTRL_MASK_PC1 | EXE_CTRL_MASK_PC2 | EXE_CTRL_MASK_PC3;
  uint8_t enable = 0; // PC3_EN   | PC2_EN   | PC1_EN   | PC0_EN   | INSTR_CNT_DOWN_EN   | UNCONDITIONAL_EN
  uint8_t frozen = 0; // PC3_TRIG | PC2_TRIG | PC1_TRIG | PC0_TRIG | INSTR_CNT_DOWN_TRIG | UNCONDITIONAL_TRIG
  bool attention = false; // the host changed the controller since the last batch boundary
  bool instr_cnt_down_pending = false; // armed, but counting starts at the next batch boundary
  reg_t tmp_instr_cnt_down = 0;
  reg_t tmp_pc_break[NUM_PC_BREAK] = {0};
  reg_t loaded_instr_cnt_down = 0; // the count down while not counting (pending, triggered or reset)
  uint64_t instr_cnt_down_break_at = 0; // while counting, the retired-instruction count at which it runs out
  reg_t loaded_pc_break[NUM_PC_BREAK] = {0};

  bool instr_cnt_down_counting() const
  {
    return (this->enable & EXE_CTRL_MASK_INSTR_CNT_DOWN) && !this->instr_cnt_down_pending;
  }

  reg_t instr_cnt_down(uint64_t retired) const
  {
    return instr_cnt_down_counting() ? this->instr_cnt_down_break_at - retired : this->loaded_instr_cnt_down;
  }

public:
  /**
   * Read a control register for the host.
   * @param retired The HART's retired-instruction count (exact at batch boundaries).
   */
  reg_t read_cr(reg_t regnum, uint64_t retired) const
  {
    switch (regnum)
    {
    case CR_EXE_CTRL_NULL:
    case CR_EXE_CTRL_RESET:
      return 0;
    case CR_EXE_CTRL_ENABLE:
      return this->enable;
    case CR_EXE_CTRL_FROZEN:
      return this->frozen;
    case CR_EXE_CTRL_INSTR_CNT_DOWN:
      return instr_cnt_down(retired);
    case CR_EXE_CTRL_PC0_BREAK:
    case CR_EXE_CTRL_PC1_BREAK:
    case CR_EXE_CTRL_PC2_BREAK:
    case CR_EXE_CTRL_PC3_BREAK:
      return loaded_pc_break[regnum - CR_EXE_CTRL_PC0_BREAK];
    default:
      throw std::runtime_error("Attempting to access an invalid HTIF execution control regnum " + std::to_string(regnum));
    }
  }

  /**
   * Write a control register for the host.
   * @param retired The HART's retired-instruction count (exact at batch boundaries).
   */
  reg_t write_cr(reg_t regnum, reg_t new_val, uint64_t retired)
  {
    reg_t old_val;
    switch (regnum)
    {
    case CR_EXE_CTRL_NULL:
    {
      old_val = 0;
      break;
    }
    case CR_EXE_CTRL_RESET:
    {
      old_val = 0;
      if (new_val & EXE_CTRL_MASK_INSTR_CNT_DOWN)
      {
        this->loaded_instr_cnt_down = instr_cnt_down(retired);
        this->instr_cnt_down_pending = false;
      }
      reg_t rst_mask = EXE_CTRL_MASK_ALL & ~new_val;
      this->enable &= rst_mask;
      this->frozen &= rst_mask;
      this->attention = true;
      break;
    }
    case CR_EXE_CTRL_ENABLE:
    {
      old_val = this->enable;
      this->enable |= EXE_CTRL_MASK_ALL & new_val;
      if (new_val & EXE_CTRL_MASK_INSTR_CNT_DOWN)
      {
        this->loaded_instr_cnt_down = this->tmp_instr_cnt_down;
        this->instr_cnt_down_pending = true;
      }
      for (size_t i = 0; i < NUM_PC_BREAK; ++i)
        if (new_val & (EXE_CTRL_MASK_PC0 << i))
          this->loaded_pc_break[i] = this->tmp_pc_break[i];
      this->attention = true;
      break;
    }

    case CR_EXE_CTRL_FROZEN:
    {
      old_val = this->frozen;
      this->frozen &= EXE_CTRL_MASK_ALL & ~new_val;
      this->attention = true;
      break;
    }
    case CR_EXE_CTRL_INSTR_CNT_DOWN:
      old_val = instr_cnt_down(retired);
      this->tmp_instr_cnt_down = new_val;
      break;
    case CR_EXE_CTRL_PC0_BREAK:
    case CR_EXE_CTRL_PC1_BREAK:
    case CR_EXE_CTRL_PC2_BREAK:
    case CR_EXE_CTRL_PC3_BREAK:
      old_val = loaded_pc_break[regnum - CR_EXE_CTRL_PC0_BREAK];
      tmp_pc_break[regnum - CR_EXE_CTRL_PC0_BREAK] = new_val;
      break;
    default:
      throw std::runtime_error("Attempting to access an invalid HTIF execution control regnum " + std::to_string(regnum));
    }
    return old_val;
  }

  /**
   * True if the host changed the controller since the last batch boundary.
   */
  bool needs_attention() const
  {
    return this->attention;
  }

  /**
   * Evaluate the triggers at a batch boundary: start a pending count down, then apply the unconditional breakpoint and
   * a count down that has run out.
   * @param retired The HART's retired-instruction count.
   * @param[out] max_retire How many instructions may retire before the count down runs out (UINT64_MAX if not counting).
   * @return True if the HART is frozen.
   */
  bool at_batch_boundary(uint64_t retired, uint64_t &max_retire)
  {
    this->attention = false;
    max_retire = UINT64_MAX;
    if (likely(!this->frozen && !this->enable)) return false;

    if (this->enable & EXE_CTRL_MASK_UNCONDITIONAL)
    {
      this->frozen |= EXE_CTRL_MASK_UNCONDITIONAL;
      this->enable &= ~EXE_CTRL_MASK_UNCONDITIONAL;
    }

    if (this->enable & EXE_CTRL_MASK_INSTR_CNT_DOWN)
    {
      if (this->instr_cnt_down_pending)
      {
        this->instr_cnt_down_break_at = retired + this->loaded_instr_cnt_down;
        this->instr_cnt_down_pending = false;
      }
      if (retired >= this->instr_cnt_down_break_at)
      {
        this->frozen |= EXE_CTRL_MASK_INSTR_CNT_DOWN;
        this->enable &= ~EXE_CTRL_MASK_INSTR_CNT_DOWN;
        this->loaded_instr_cnt_down = 0;
      }
      else
      {
        max_retire = this->instr_cnt_down_break_at - retired;
      }
    }

    return this->frozen;
  }

  /**
   * True if an armed PC breakpoint is at pc. Called when the instruction at pc is decoded into the icache.
   */
  bool is_pc_break(reg_t pc) const
  {
    if (likely(!(this->enable & PC_BREAK_MASK_ALL))) return false;
    for (size_t i = 0; i < NUM_PC_BREAK; ++i)
      if ((this->enable & (EXE_CTRL_MASK_PC0 << i)) && pc == this->loaded_pc_break[i])
        return true;
    return false;
  }

  /**
   * Freeze the HART for the armed PC breakpoint(s) at pc, which are disarmed.
   */
  void trigger_pc_break(reg_t pc)
  {
    for (size_t i = 0; i < NUM_PC_BREAK; ++i)
    {
      reg_t PCn_BREAK_MASK = (EXE_CTRL_MASK_PC0 << i);
      if ((this->enable & PCn_BREAK_MASK) && pc == this->loaded_pc_break[i])
      {
        this->frozen |= PCn_BREAK_MASK;
        this->enable &= ~PCn_BREAK_MASK;
      }
    }
  }
};

#endif //_PROCESSOR_EXECUTION_CONTROLLER
