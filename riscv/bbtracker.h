#ifndef BBTRACKER_H
#define BBTRACKER_H

#include <cinttypes>
#include <string>
#include <vector>
#include "fesvr/gzstream.h"

/* Basic block vector (BBV) profile for the SimPoint tool.

   A basic block is identified by the pc of its last instruction, a control-flow instruction. Blocks get ids in the
   order they are first seen, and each interval of interval_size instructions writes a line of the non-zero counts of
   that interval, by id: "T:<id+1>:<count>   ...". */
class bb_tracker_t {

private:
  /* Open-addressing hash table of the blocks, by pc, with their instruction count in the current interval (but what
     the cache below holds). Its capacity is a power of two, and it is kept at most half full, so that probes are
     short. */
  struct slot_t {
    uint64_t pc; // EMPTY_PC if the slot is free (a pc is never odd)
    uint64_t count;
  };
  static constexpr uint64_t EMPTY_PC = UINT64_MAX;
  std::vector<slot_t> table;
  std::vector<uint64_t> table_ids; // the id of the block in each slot, only needed for a dump
  uint64_t table_mask;
  uint64_t num_bbs;

  std::vector<uint64_t> counts_by_id; // a dump's counts, by id

  /* A small direct-mapped cache in front of the table, indexed by pc, so that the blocks of a hot loop, which are close
     in the code, are counted in a few cache lines (the table's hash spreads them). It accumulates their counts, and
     adds them to their table slot when evicted, before a dump and before the table grows. A block still gets its id
     when first seen. */
  struct cached_t {
    uint64_t pc; // EMPTY_PC if free
    uint64_t count; // not yet added to the table
    uint64_t index; // the block's table slot
  };
  static constexpr uint64_t CACHE_ENTRIES = 4096;
  std::vector<cached_t> cache;

  std::string finalname;
  ogzstream bbtrace;

  uint64_t interval_size;

  uint64_t dyn_inst;
  uint64_t total_inst;

  static uint64_t hash(uint64_t pc) { return ((pc >> 2) * UINT64_C(0x9E3779B97F4A7C15)) >> 32; }

  uint64_t find_or_add_bb(uint64_t pc);

  void cache_miss(cached_t &c, uint64_t pc, uint64_t num_inst);

  void flush_cache();

  void grow_table();

  void print_bb_counts();

public:

  bb_tracker_t();

  ~bb_tracker_t();

  void init_bb_tracker(const char *m_dir_name, const char *m_out_name);

  void set_interval_size(uint64_t m_interval_size);

  /* Called at each CTRL op, marking the end of a basic block.  The pc of the last
   instruction indexes into the basic block hash, and the counter is incremented
   by the number of instructions in the basic block. Return true on each stats dump. */
  bool bb_tracker(uint64_t pc, uint64_t num_inst)
  {
    cached_t &c = cache[(pc >> 2) & (CACHE_ENTRIES - 1)];
    if (__builtin_expect(c.pc == pc, 1))
      c.count += num_inst;
    else
      cache_miss(c, pc, num_inst);

    dyn_inst += num_inst;
    total_inst += num_inst;

    /* if reached end of interval, dump stats and decrement counter */
    if (__builtin_expect(dyn_inst > interval_size, 0)) {
      dyn_inst -= interval_size;
      print_bb_counts();
      return true;
    }
    return false;
  }
};

#endif
