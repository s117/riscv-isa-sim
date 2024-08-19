//
// Created by john on 7/28/24.
//

#ifndef _PROCESSOR_EXECUTION_CONTROLLER
#define _PROCESSOR_EXECUTION_CONTROLLER

#include "fesvr/hart_execution_reg.h"
#include "decode.h"
#include <cassert>
#include <stdexcept>

class core_frozen_t : public std::exception
{
};

class processor_execution_controller_t
{
private:
  constexpr static size_t NUM_PC_BREAK = 4;
  uint8_t enable = 0; // PC3_EN   | PC2_EN   | PC1_EN   | PC0_EN   | INSTR_CNT_DOWN_EN
  uint8_t frozen = 0; // PC3_TRIG | PC2_TRIG | PC1_TRIG | PC0_TRIG | INSTR_CNT_DOWN_TRIG
  reg_t tmp_instr_cnt_down = 0;
  reg_t tmp_pc_break[NUM_PC_BREAK] = {0};
  reg_t loaded_instr_cnt_down = 0;
  reg_t loaded_pc_break[NUM_PC_BREAK] = {0};

public:
  reg_t read_cr(reg_t regnum)
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
      return this->loaded_instr_cnt_down;
    case CR_EXE_CTRL_PC0_BREAK:
    case CR_EXE_CTRL_PC1_BREAK:
    case CR_EXE_CTRL_PC2_BREAK:
    case CR_EXE_CTRL_PC3_BREAK:
      return loaded_pc_break[regnum - CR_EXE_CTRL_PC0_BREAK];
    default:
      throw std::runtime_error("Attempting to access an invalid HTIF execution control regnum " + std::to_string(regnum));
    }
  }

  reg_t write_cr(reg_t regnum, reg_t new_val)
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
      reg_t rst_mask = EXE_CTRL_MASK_ALL & ~new_val;
      this->enable &= rst_mask;
      this->frozen &= rst_mask;
      break;
    }
    case CR_EXE_CTRL_ENABLE:
    {
      old_val = this->enable;
      this->enable |= EXE_CTRL_MASK_ALL & new_val;
      if (new_val & EXE_CTRL_MASK_INSTR_CNT_DOWN)
        this->loaded_instr_cnt_down = this->tmp_instr_cnt_down;
      for (size_t i = 0; i < NUM_PC_BREAK; ++i)
        if (new_val & (EXE_CTRL_MASK_PC0 << i))
          this->loaded_pc_break[i] = this->tmp_pc_break[i];
      break;
    }

    case CR_EXE_CTRL_FROZEN:
    {
      old_val = this->frozen;
      this->frozen &= EXE_CTRL_MASK_ALL & ~new_val;
      break;
    }
    case CR_EXE_CTRL_INSTR_CNT_DOWN:
      old_val = this->loaded_instr_cnt_down;
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

  void pre_execution_check(reg_t pc)
  {
    // check unconditional breakpoint
    if (this->enable & EXE_CTRL_MASK_UNCONDITIONAL)
    {
      this->frozen |= EXE_CTRL_MASK_UNCONDITIONAL;
      this->enable &= ~EXE_CTRL_MASK_UNCONDITIONAL;
    }

    // check instruction count down breakpoint
    if ((this->enable & EXE_CTRL_MASK_INSTR_CNT_DOWN) && (this->loaded_instr_cnt_down == 0))
    {
      this->frozen |= EXE_CTRL_MASK_INSTR_CNT_DOWN;
      this->enable &= ~EXE_CTRL_MASK_INSTR_CNT_DOWN;
    }

    // check PC breakpoint
    for (size_t i = 0; i < NUM_PC_BREAK; ++i)
    {
      reg_t PCn_BREAK_MASK = (EXE_CTRL_MASK_PC0 << i);
      if ((this->enable & PCn_BREAK_MASK) && (pc == this->loaded_pc_break[i]))
      {
        this->frozen |= PCn_BREAK_MASK;
        this->enable &= ~PCn_BREAK_MASK;
      }
    }

    // when frozen, use core_frozen_t exception to halt the harts simulation until defrost
    if (this->frozen) throw core_frozen_t();
  }

  void post_execution_check(reg_t npc)
  {
    if (this->frozen)
      throw std::runtime_error("Invalid HTIF execution control state invalid: instruction retired while harts is frozen (frozen=" + std::to_string(this->frozen) + ").");
  }

  void on_instret_increment() {
    // update instruction count down.
    if ((this->enable & EXE_CTRL_MASK_INSTR_CNT_DOWN)) {
      assert(this->loaded_instr_cnt_down > 0);
      --this->loaded_instr_cnt_down;
    }
  }

  uint8_t frozen_state() {
    return this->frozen;
  }
};

#endif //_PROCESSOR_EXECUTION_CONTROLLER
