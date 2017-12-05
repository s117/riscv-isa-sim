// See LICENSE for license details.

#ifndef _HTIF_H
#define _HTIF_H

#include <fesvr/htif_pthread.h>
#include <fstream>
//#include <gzstream.h>

class sim_t;
struct packet;

typedef enum {READ_MEM, MOD_SCR} restore_cmd_t;

typedef struct replay_pkt
{

  restore_cmd_t command;
  reg_t addr;
  reg_t data_size;
  reg_t data[256];
  reg_t coreid;
  reg_t regno;
  reg_t old_regval;
  reg_t new_regval;
  
  void dump()
  {
    fprintf(stderr,"command: %s addr: %ld data_size: %ld coreid: %ld regno: %ld old_regval: %ld\n",
                    command == READ_MEM ? "READ_MEM" : "MOD_SCR", 
                    addr, data_size, coreid, regno, old_regval);
  };
} replay_pkt_t;


// this class implements the host-target interface for program loading, etc.
// a simpler implementation would implement the high-level interface
// (read/write cr, read/write chunk) directly, but we implement the lower-
// level serialized interface to be more similar to real target machines.

class htif_isasim_t : public htif_pthread_t
{
public:
  htif_isasim_t(sim_t* _sim, const std::vector<std::string>& args);
  ~htif_isasim_t();
  bool tick();
  bool done();
  bool restore_checkpoint(std::istream& restore);
  void start_checkpointing(std::ostream& checkpoint_file);
  void stop_checkpointing();

private:
  sim_t* sim;
  bool reset;
  uint8_t seqno;
  void setup_replay_state(replay_pkt_t*);
  bool checkpointing_active;

  //std::fstream* checkpoint;
  std::ostream* checkpoint;

  void tick_once();
};

#endif
