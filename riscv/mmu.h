// See LICENSE for license details.

#ifndef _RISCV_MMU_H
#define _RISCV_MMU_H

#include "decode.h"
#include "trap.h"
#include "common.h"
#include "config.h"
#include "processor.h"
#include "memtracer.h"
#include "debug_tracer.h"
#include <vector>

// virtual memory configuration
typedef reg_t pte_t;
const reg_t LEVELS = sizeof(pte_t) == 8 ? 3 : 2;
const reg_t PTIDXBITS = 10;
const reg_t PGSHIFT = PTIDXBITS + (sizeof(pte_t) == 8 ? 3 : 2);
const reg_t PGSIZE = 1 << PGSHIFT;
const reg_t VPN_BITS = PTIDXBITS * LEVELS;
const reg_t PPN_BITS = 8*sizeof(reg_t) - PGSHIFT;
const reg_t VA_BITS = VPN_BITS + PGSHIFT;

struct insn_fetch_t
{
  insn_func_t func;
  insn_t insn;
};

struct icache_entry_t {
  reg_t tag;
  reg_t pad;
  insn_fetch_t data;
};

// this class implements a processor's port into the virtual memory system.
// an MMU and instruction cache are maintained for simulator performance.
class mmu_t
{
public:
  mmu_t(char* _mem, size_t _memsz);
  ~mmu_t();
#ifdef RISCV_ENABLE_DBG_TRACE
  // template for functions that load an aligned value from memory
  #define load_func(type) \
    type##_t load_##type(reg_t addr) __attribute__((always_inline)) { \
      if (likely(insn_tracer != nullptr)) \
        insn_tracer->trace_before_dc_translate(addr, sizeof(type##_t), false); \
      void* paddr = translate(addr, sizeof(type##_t), false, false); \
      auto load_val = *(type##_t*)paddr; \
      if (likely(insn_tracer != nullptr)) \
        insn_tracer->trace_after_dc_access(addr, ((uintptr_t)paddr - (uintptr_t)mem), load_val, sizeof(type##_t), false); \
      return load_val; \
    }

  // template for functions that store an aligned value to memory
  #define store_func(type) \
    void store_##type(reg_t addr, type##_t val) { \
      if (likely(insn_tracer != nullptr)) \
        insn_tracer->trace_before_dc_translate(addr, sizeof(type##_t), true); \
      void* paddr = translate(addr, sizeof(type##_t), true, false); \
      *(type##_t*)paddr = val; \
      if (likely(insn_tracer != nullptr)) \
        insn_tracer->trace_after_dc_access(addr, ((uintptr_t)paddr - (uintptr_t)mem), val, sizeof(type##_t), true); \
    }
#else
  // template for functions that load an aligned value from memory
  #define load_func(type) \
    type##_t load_##type(reg_t addr) __attribute__((always_inline)) { \
      void* paddr = translate(addr, sizeof(type##_t), false, false); \
      return *(type##_t*)paddr; \
    }

  // template for functions that store an aligned value to memory
  #define store_func(type) \
    void store_##type(reg_t addr, type##_t val) { \
      void* paddr = translate(addr, sizeof(type##_t), true, false); \
      *(type##_t*)paddr = val; \
    }
#endif

  // load value from memory at aligned address; zero extend to register width
  load_func(uint8)
  load_func(uint16)
  load_func(uint32)
  load_func(uint64)

  // load value from memory at aligned address; sign extend to register width
  load_func(int8)
  load_func(int16)
  load_func(int32)
  load_func(int64)

  // store value to memory at aligned address
  store_func(uint8)
  store_func(uint16)
  store_func(uint32)
  store_func(uint64)

  static const reg_t ICACHE_ENTRIES = 1024;

  inline size_t icache_index(reg_t addr)
  {
    // for instruction sizes != 4, this hash still works but is suboptimal
    return (addr / 4) % ICACHE_ENTRIES;
  }

  // load instruction from memory at aligned address.
  icache_entry_t* access_icache(reg_t addr) __attribute__((always_inline))
  {
#ifdef RISCV_ENABLE_DBG_TRACE
//    if (likely(insn_tracer != nullptr))
      insn_tracer->trace_before_insn_ic_fetch(addr);
#endif

    reg_t idx = icache_index(addr);
    icache_entry_t* entry = &icache[idx];
    if (likely(entry->tag == addr))
      return entry;

    bool rvc = false; // set this dynamically once RVC is re-implemented
    char* iaddr = (char*)translate(addr, rvc ? 2 : 4, false, true);
    insn_bits_t insn = *(uint16_t*)iaddr;

    if (unlikely(insn_length(insn) == 2)) {
      insn = (int16_t)insn;
    } else if (likely(insn_length(insn) == 4)) {
      if (likely((addr & (PGSIZE-1)) < PGSIZE-2))
        insn |= (insn_bits_t)*(int16_t*)(iaddr + 2) << 16;
      else
        insn |= (insn_bits_t)*(int16_t*)translate(addr + 2, 2, false, true) << 16;
    } else if (insn_length(insn) == 6) {
      insn |= (insn_bits_t)*(int16_t*)translate(addr + 4, 2, false, true) << 32;
      insn |= (insn_bits_t)*(uint16_t*)translate(addr + 2, 2, false, true) << 16;
    } else {
      static_assert(sizeof(insn_bits_t) == 8, "insn_bits_t must be uint64_t");
      insn |= (insn_bits_t)*(int16_t*)translate(addr + 6, 2, false, true) << 48;
      insn |= (insn_bits_t)*(uint16_t*)translate(addr + 4, 2, false, true) << 32;
      insn |= (insn_bits_t)*(uint16_t*)translate(addr + 2, 2, false, true) << 16;
    }

    insn_fetch_t fetch = {proc->decode_insn(insn), insn};
    icache[idx].tag = addr;
    icache[idx].data = fetch;

    reg_t paddr = iaddr - mem;
    if (!tracer.empty() && tracer.interested_in_range(paddr, paddr + 1, false, true))
    {
      icache[idx].tag = -1;
      tracer.trace(paddr, 1, false, true);
    }
    return &icache[idx];
  }

  inline insn_fetch_t load_insn(reg_t addr)
  {
    return access_icache(addr)->data;
  }

  void set_processor(processor_t* p) {
    proc = p;
#ifdef RISCV_ENABLE_DBG_TRACE
    if (p)
      insn_tracer = p->get_dbg_tracer();
#endif
    flush_tlb();
  }


  void flush_tlb();
  void flush_icache();

  void register_memtracer(memtracer_t*);

private:
  char* mem;
  size_t memsz;
  processor_t* proc;
  memtracer_list_t tracer;
#ifdef RISCV_ENABLE_DBG_TRACE
  debug_tracer_t* insn_tracer;
#endif

  // implement an instruction cache for simulator performance
  icache_entry_t icache[ICACHE_ENTRIES];

  // implement a TLB for simulator performance
  static const reg_t TLB_ENTRIES = 256;
  char* tlb_data[TLB_ENTRIES];
  reg_t tlb_insn_tag[TLB_ENTRIES];
  reg_t tlb_load_tag[TLB_ENTRIES];
  reg_t tlb_store_tag[TLB_ENTRIES];

  // finish translation on a TLB miss and upate the TLB
  void* refill_tlb(reg_t addr, reg_t bytes, bool store, bool fetch);

  // perform a page table walk for a given virtual address
  pte_t walk(reg_t addr);

  // translate a virtual address to a physical address
  void* translate(reg_t addr, reg_t bytes, bool store, bool fetch)
    __attribute__((always_inline))
  {
    reg_t idx = (addr >> PGSHIFT) % TLB_ENTRIES;
    reg_t expected_tag = addr >> PGSHIFT;
    reg_t* tags = fetch ? tlb_insn_tag : store ? tlb_store_tag :tlb_load_tag;
    reg_t tag = tags[idx];
    void* data = tlb_data[idx] + addr;

    if (unlikely(addr & (bytes-1)))
      store ? throw trap_store_address_misaligned(addr) :
      fetch ? throw trap_instruction_address_misaligned(addr) :
      throw trap_load_address_misaligned(addr);

    if (likely(tag == expected_tag))
      return data;

    return refill_tlb(addr, bytes, store, fetch);
  }

  friend class processor_t;
};

#endif
