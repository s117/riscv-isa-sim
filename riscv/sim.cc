// See LICENSE for license details.

#include "sim.h"
#include "htif.h"
#include <map>
#include <iostream>
#include <climits>
#include <cstdlib>
#include <cassert>
#include <signal.h>
#include <iostream>
#include <fstream>
#include <gzstream.h>

bool logging_on             = false;

volatile bool ctrlc_pressed = false;
static void handle_signal(int sig)
{
  if (ctrlc_pressed)
    exit(-1);
  ctrlc_pressed = true;
  signal(sig, &handle_signal);
}

sim_t::sim_t(size_t nprocs, size_t mem_mb, const std::vector<std::string>& args)
  : htif(new htif_isasim_t(this, args)), procs(std::max(nprocs, size_t(1))),
    current_step(0), current_proc(0), debug(false), checkpointing_enabled(false)
{
  signal(SIGINT, &handle_signal);
  // allocate target machine's memory, shrinking it as necessary
  // until the allocation succeeds
  size_t memsz0 = (size_t)mem_mb << 20;
  size_t quantum = 1L << 20;
  if (memsz0 == 0)
    memsz0 = 1L << (sizeof(size_t) == 8 ? 32 : 30);

  memsz = memsz0;
  fprintf(stderr, "Requesting target memory 0x%lx\n",(unsigned long)memsz0);
  while ((mem = (char*)calloc(1, memsz)) == NULL)
    memsz = memsz*10/11/quantum*quantum;

  if (memsz != memsz0)
    fprintf(stderr, "warning: only got %lu bytes of target mem (wanted %lu)\n",
            (unsigned long)memsz, (unsigned long)memsz0);

  debug_mmu = new mmu_t(mem, memsz);

  for (size_t i = 0; i < procs.size(); i++) {
    procs[i] = new processor_t(this, new mmu_t(mem, memsz), i);
  }

}

sim_t::~sim_t()
{
  for (size_t i = 0; i < procs.size(); i++)
  {
    mmu_t* pmmu = procs[i]->get_mmu();
    delete procs[i];
    delete pmmu;
  }
  delete debug_mmu;
  free(mem);
}

void sim_t::send_ipi(reg_t who)
{
  if (who < procs.size())
    procs[who]->deliver_ipi();
}

reg_t sim_t::get_scr(int which)
{
  switch (which)
  {
    case 0: return procs.size();
    case 1: return memsz >> 20;
    default: return -1;
  }
}

void sim_t::boot()
{
  // This tick will initialize the processor.
	bool htif_return = htif->tick();

}

int sim_t::run()
{
  while (htif->tick())
  {
    if (debug || ctrlc_pressed)
      interactive();
    else
      step(INTERLEAVE);
  }
  return htif->exit_code();
}

void sim_t::step(size_t n)
{
  for (size_t i = 0, steps = 0; i < n; i += steps)
  {
    steps = std::min(n - i, INTERLEAVE - current_step);
    procs[current_proc]->step(steps);

    current_step += steps;
    if (current_step == INTERLEAVE)
    {
      current_step = 0;
      procs[current_proc]->yield_load_reservation();
      if (++current_proc == procs.size())
        current_proc = 0;

      htif->tick();
    }
  }
}

// Currently supports only one core - can be easily extended to all cores
bool sim_t::run(size_t n)
{
  bool htif_return = true;
  size_t total_retired = 0;
  size_t steps = 0;
  size_t instret = 0;
  while(total_retired < n && htif_return)
	{
		steps = std::min(n - total_retired, INTERLEAVE - current_step);

    // This function continues until it has retired "steps" instructions
    // or it encounters a trap.
    instret = procs[current_proc]->step(steps);

    total_retired += instret;
		current_step += steps;
    // Either the core has retired INTERLEAVE number of instructions
    // or it has been idle for a INTERLEAVE steps, do a HTIF tick and move to
    // the next core.
		if (current_step == INTERLEAVE)
		{
			current_step = 0;//current_step % INTERLEAVE;
			procs[current_proc]->yield_load_reservation();
			if (++current_proc == procs.size()) {
				current_proc = 0;
			}

      // If HTIF is done, this will return false
			htif_return = htif->tick();
		}
	}
  return htif_return;
}

bool sim_t::running()
{
  for (size_t i = 0; i < procs.size(); i++)
    if (procs[i]->running())
      return true;
  return false;
}

void sim_t::stop()
{
  procs[0]->state.tohost = 1;
  while (htif->tick())
    ;
}

void sim_t::set_debug(bool value)
{
  debug = value;
}

void sim_t::set_histogram(bool value)
{
  histogram_enabled = value;
  for (size_t i = 0; i < procs.size(); i++) {
    procs[i]->set_histogram(histogram_enabled);
  }
}

#ifdef RISCV_ENABLE_SIMPOINT
void sim_t::set_simpoint(bool enable, size_t interval)
{
  for (size_t i = 0; i < procs.size(); i++) {
    procs[i]->set_simpoint(enable, interval);
  }
}
#endif

#ifdef RISCV_ENABLE_DBG_TRACE
void sim_t::enable_trace(size_t n)
{
  for (size_t i = 0; i < procs.size(); i++) {
    procs[i]->enable_trace(n);
  }
}
#endif

void sim_t::set_procs_debug(bool value)
{
  for (size_t i=0; i< procs.size(); i++)
    procs[i]->set_debug(value);
}

void sim_t::init_checkpoint()
{
  checkpointing_enabled = true;
  htif->start_checkpointing();
}

bool sim_t::create_checkpoint(std::string checkpoint_file)
{
  bool htif_return = true;
  // Check if file name has .gz extension. If not, append .gz to the name
  if(checkpoint_file.substr(checkpoint_file.find_last_of(".") + 1) != "gz") {
    checkpoint_file = checkpoint_file+".gz";
  }

  proc_chkpt.open(checkpoint_file.c_str(), std::ios::out | std::ios::binary);
  if ( ! proc_chkpt.good()) {
    std::cerr << "ERROR: Opening file `" << checkpoint_file << "' failed.\n";
    exit(0);
  }
  htif->output_checkpointing(proc_chkpt);
  fprintf(stderr,"Checkpointed HTIF state\n");
  fflush(0);

  create_memory_checkpoint(proc_chkpt);
  fprintf(stderr,"Checkpointed memory state\n");
  fflush(0);

  create_register_checkpoint(proc_chkpt);
  fprintf(stderr,"Checkpointed register state\n");
  fflush(0);

  proc_chkpt.close();
  std::cerr << "Created processor checkpoint to " << checkpoint_file << std::endl;
  return htif_return;
}

bool sim_t::restore_checkpoint(std::string restore_file)
{
  bool htif_return = true;

  // Check if file name has .gz extension. If not, append .gz to the name
  if(restore_file.substr(restore_file.find_last_of(".") + 1) != "gz") {
    restore_file = restore_file+".gz";
  }

  //std::cerr << "Trying to restore HTIF checkpoint from " << restore_file << std::endl;
  fflush(0);
  restore_chkpt.open (restore_file.c_str(), std::ios::in | std::ios::binary);
  if ( ! proc_chkpt.good()) {
    std::cerr << "ERROR: Opening file `" << restore_file << "' failed.\n";
	  return false;
  }

  // This tick will restore the checkpoint.
	htif_return = htif->restore_checkpoint(restore_chkpt);
  std::cerr << "Done restoring HTIF checkpoint from " << restore_file << std::endl;

  //std::cerr << "Trying to restore mem/reg HTIF checkpoint from " << restore_file << std::endl;
  restore_memory_checkpoint(restore_chkpt);
  restore_proc_checkpoint(restore_chkpt);
  restore_chkpt.close();
  std::cerr << "Done restoring mem/reg checkpoint from " << restore_file << std::endl;

  return htif_return;
}

void sim_t::create_memory_checkpoint(std::ostream& memory_chkpt)
{
  uint64_t signature = 0xbaadbeefdeadbeef;
  memory_chkpt.write((char*)&signature,8);
  memory_chkpt.write((char*)&memsz,sizeof(memsz));
  memory_chkpt.write(mem,memsz);
}

void sim_t::create_register_checkpoint(std::ostream& proc_chkpt)
{
  state_t *state = procs[current_proc]->get_state();
  uint64_t signature = 0xdeadbeefbaadbeef;
  proc_chkpt.write((char*)&signature,8);
  proc_chkpt.write((char *)state,sizeof(state_t));
}

void sim_t::restore_memory_checkpoint(std::istream& memory_chkpt)
{
  uint64_t signature;
  uint64_t chkpt_memsz;
  memory_chkpt.read((char*)&signature,8);
  assert(signature == 0xbaadbeefdeadbeef);
  // Check that the checkpointed memory size the current simulator memory size are same
  memory_chkpt.read((char*)&chkpt_memsz,sizeof(chkpt_memsz));
  assert(memsz == chkpt_memsz);
  memory_chkpt.read(mem,memsz);
}

void sim_t::restore_proc_checkpoint(std::istream& proc_chkpt)
{
  state_t *state = procs[0]->get_state();
  uint64_t signature;
  proc_chkpt.read((char*)&signature,8);
  assert(signature == 0xdeadbeefbaadbeeful);
  proc_chkpt.read((char *)state,sizeof(state_t));
}
