#ifndef BBTRACKER_H
#define BBTRACKER_H

#include <cinttypes>
#include <string>
#include "gzstream.h"

/* Size of basic block hash table. Should be increased for very 
   large programs (greater than 1 million basic blocks) */
#define bb_size 1000000

/* basic block element */
typedef struct node {
  uint64_t count;
  uint64_t bb_id;
  uint64_t pc;
  struct node *next;
} bb_node;

typedef bb_node *bb_node_ptr;

class bb_tracker_t {

private:
  bb_node_ptr bb_hash[bb_size];

  uint64_t bb_id_pool = bb_size;
  uint64_t bb_id;

  std::string finalname;
  ogzstream bbtrace;

  uint64_t interval_size;
  uint64_t interval_sum;

  uint64_t dyn_inst;
  uint64_t total_inst;
  uint64_t total_calls;

  bb_node_ptr create_bb_node(uint64_t pc, uint64_t num_inst);

  void append_bb_node(bb_node_ptr m_bb_node, bb_node_ptr head);

  int find_bb_node(bb_node_ptr head, uint64_t pc, uint64_t num_inst);

  void print_list(bb_node_ptr head, uint64_t array[]);

  void print_bb_hash();

public:

  bb_tracker_t();

  ~bb_tracker_t();

  void init_bb_tracker(const char *m_dir_name, const char *m_out_name);

  void set_interval_size(uint64_t m_interval_size);

  /* Called at each CTRL op, marking the end of a basic block.  The pc of the last
   instruction indexes into the basic block hash, and the counter is incremented
   by the number of instructions in the basic block. Return true on each stats dump. */
  bool bb_tracker(uint64_t m_pc, uint64_t m_num_inst);
};

#endif

