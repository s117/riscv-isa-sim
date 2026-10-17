
#include <stdlib.h>
#include "bbtracker.h"

bb_tracker_t::bb_tracker_t()
  : table(size_t(1) << 16, slot_t{EMPTY_PC, 0}), table_ids(size_t(1) << 16), table_mask((uint64_t(1) << 16) - 1),
    num_bbs(0), cache(CACHE_ENTRIES, cached_t{EMPTY_PC, 0, 0}) {

  interval_size = 0;

  dyn_inst = 0;
  total_inst = 0;

}

bb_tracker_t::~bb_tracker_t() {
  if (!bbtrace.good()) {
    std::cerr << "Fail to dump the BBV for simpoint: bad output stream state" << std::endl;
  }
  bbtrace.close();
}


void bb_tracker_t::init_bb_tracker(const char *dir_name, const char *out_name) {
  finalname = std::string(dir_name) + "/" + out_name + ".bb.gz";
  bbtrace.open(finalname.c_str());
  if (!bbtrace.good()) {
    std::cerr << "SimPoint output error: fail to open BB Vector output file" << finalname << std::endl;
    exit(1);
  }
}


/* The table slot of the block ending at pc. A block not seen yet gets the next id. */
uint64_t bb_tracker_t::find_or_add_bb(uint64_t pc) {
  uint64_t index = hash(pc) & table_mask;
  while (table[index].pc != pc) {
    if (table[index].pc == EMPTY_PC) {
      /* keep the table at most half full: grow it first (which flushes the cache), then find the free slot again */
      if ((num_bbs + 1) * 2 > table.size()) {
        grow_table();
        return find_or_add_bb(pc);
      }
      table[index] = slot_t{pc, 0};
      table_ids[index] = num_bbs++;
      break;
    }
    index = (index + 1) & table_mask;
  }
  return index;
}


/* The block ending at pc isn't in its cache entry c: add the evicted block's count to the table, and cache this one */
void bb_tracker_t::cache_miss(cached_t &c, uint64_t pc, uint64_t num_inst) {
  if (c.pc != EMPTY_PC)
    table[c.index].count += c.count;
  c.pc = EMPTY_PC; // find_or_add_bb() may flush the cache
  const uint64_t index = find_or_add_bb(pc);
  c = cached_t{pc, num_inst, index};
}


/* Add the cached counts to the table */
void bb_tracker_t::flush_cache() {
  for (cached_t &c : cache) {
    if (c.pc != EMPTY_PC) {
      table[c.index].count += c.count;
      c.count = 0;
    }
  }
}


void bb_tracker_t::grow_table() {
  /* the cached slot indexes become stale */
  flush_cache();
  for (cached_t &c : cache)
    c.pc = EMPTY_PC;

  std::vector<slot_t> old(table.size() * 2, slot_t{EMPTY_PC, 0});
  std::vector<uint64_t> old_ids(table.size() * 2);
  old.swap(table);
  old_ids.swap(table_ids);
  table_mask = table.size() - 1;

  for (size_t i = 0; i < old.size(); i++) {
    if (old[i].pc == EMPTY_PC)
      continue;
    uint64_t index = hash(old[i].pc) & table_mask;
    while (table[index].pc != EMPTY_PC)
      index = (index + 1) & table_mask;
    table[index] = old[i];
    table_ids[index] = old_ids[i];
  }
}


void bb_tracker_t::print_bb_counts() {
  /* sort the counts by block id, and clear them */
  flush_cache();
  counts_by_id.assign(num_bbs, 0);
  for (size_t i = 0; i < table.size(); i++) {
    if (table[i].pc != EMPTY_PC && table[i].count > 0) {
      counts_by_id[table_ids[i]] = table[i].count;
      table[i].count = 0;
    }
  }

  bbtrace << "T";

  for (uint64_t i = 0; i < num_bbs; i++) {
    if (counts_by_id[i] > 0) {
      bbtrace << ":" << i + 1 << ":" << counts_by_id[i] << "   ";
    }
  }

  bbtrace << "\n";
}


void bb_tracker_t::set_interval_size(uint64_t m_interval_size) {
  interval_size = m_interval_size;
}
