// See LICENSE for license details.

#include "sim.h"
#include "htif.h"
#include <map>
#include <iostream>
#include <climits>
#include <cstdlib>
#include <cassert>
#include <signal.h>

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
  bool old_debug = get_procs_debug();

  bool htif_return = true;
  size_t total_retired = 0;
  size_t steps = 0;
  while(total_retired < n && htif_return)
	{
		steps = std::min(n - total_retired, INTERLEAVE - current_step);

    // This function continues until it has retired "steps" instructions
    // or it encounters a cycle with 0 retired instructions.
  	procs[current_proc]->step(steps);

    total_retired += steps;
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

void sim_t::set_simpoint(bool enable, size_t interval)
{
  for (size_t i = 0; i < procs.size(); i++) {
    procs[i]->set_simpoint(enable, interval);
  }
}

void sim_t::set_procs_debug(bool value)
{
  for (size_t i=0; i< procs.size(); i++)
    procs[i]->set_debug(value);
}

void sim_t::init_checkpoint(std::string _checkpoint_file)
{
  checkpointing_enabled = true; 
  checkpoint_file = _checkpoint_file;
  htif->start_checkpointing(checkpoint_file+".syscall");
}

bool sim_t::create_checkpoint()
{
  bool htif_return = true;

  htif->stop_checkpointing();
  create_memory_checkpoint(checkpoint_file+".memory");
  create_proc_checkpoint(checkpoint_file+".proc");

  return htif_return;
}

bool sim_t::restore_checkpoint(std::string restore_file)
{
  bool htif_return = true;

  fprintf(stderr,"Trying to restore checkpoint from %s.*\n",restore_file.c_str());
  // This tick will restore the checkpoint.
	htif_return = htif->restore_checkpoint(restore_file+".syscall");
  restore_memory_checkpoint(restore_file+".memory");
  restore_proc_checkpoint(restore_file+".proc");

  fprintf(stderr,"Done restoring checkpoint from %s.*\n",restore_file.c_str());

  return htif_return;
}

void sim_t::create_memory_checkpoint(std::string memory_file)
{

  std::fstream memory_chkpt;
  memory_chkpt.open (memory_file, std::ios::out | std::ios::binary);
  //uint64_t buf[1024];
  //for (size_t i = 0; i < 0x80000; i++){
  //  for (size_t j = 0; i < 1024; i++){
  //    buf[j] = debug_mmu->load_uint64((i*1024)+(j*8));
  //  }
  //  memory_chkpt.write((char *)&buf[0],1024*8);
  //}
  uint64_t signature = 0xbaadbeefdeadbeef;
  memory_chkpt.write((char*)&signature,8);
  memory_chkpt.write(mem,memsz);
  memory_chkpt.close();
}

void sim_t::create_proc_checkpoint(std::string proc_file)
{
  std::fstream proc_chkpt;
  proc_chkpt.open (proc_file, std::ios::out | std::ios::binary);
  state_t *state = procs[current_proc]->get_state();
  uint64_t signature = 0xdeadbeefbaadbeef;
  proc_chkpt.write((char*)&signature,8);
  proc_chkpt.write((char *)state,sizeof(state_t));
  proc_chkpt.close();

  fprintf(stderr,"Checkpointed State for %s:\n",proc_type == MICRO_SIM ? "micro_sim" : "isa_sim");
  procs[current_proc]->get_state()->dump(stderr);
}

void sim_t::restore_memory_checkpoint(std::string memory_file)
{
  fprintf(stderr,"Trying to restore memory state from %s\n",memory_file.c_str());
  fflush(0);
  std::fstream memory_chkpt;
  memory_chkpt.open (memory_file, std::ios::in | std::ios::binary);
  //uint64_t buf[1024];
  //for (size_t i = 0; i < 0x80000; i++){
  //  memory_chkpt.read((char *)&buf[0],1024*8);
  //  for (size_t j = 0; i < 1024; i++){
  //    debug_mmu->store_uint64(((i*1024)+(j*8)),buf[j]);
  //  }
  //}
  uint64_t signature;
  memory_chkpt.read((char*)&signature,8);
  assert(signature == 0xbaadbeefdeadbeef);
  memory_chkpt.read(mem,memsz);
  memory_chkpt.close();
  fprintf(stderr,"Done restoring memory state from %s\n",memory_file.c_str());
}

void sim_t::restore_proc_checkpoint(std::string proc_file)
{
  fprintf(stderr,"Trying to restore proc state from %s\n",proc_file.c_str());
  fflush(0);
  std::fstream proc_chkpt;
  proc_chkpt.open (proc_file, std::ios::in | std::ios::binary);
  state_t *state = procs[0]->get_state();
  uint64_t signature;
  proc_chkpt.read((char*)&signature,8);
  assert(signature = 0xdeadbeefbaadbeef);
  proc_chkpt.read((char *)state,sizeof(state_t));
  proc_chkpt.close();

  fprintf(stderr,"State for %s:\n","isa_sim");
  procs[current_proc]->get_state()->dump(stderr);

  fprintf(stderr,"Done restoring proc state from %s\n",proc_file.c_str());
}

