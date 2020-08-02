// See LICENSE for license details.

#include "htif.h"
#include "sim.h"
#include "encoding.h"
#include <unistd.h>
#include <stdexcept>
#include <stdlib.h>
#include <errno.h>
#include <assert.h>
#include <stddef.h>
#include <poll.h>
//#define __STDC_FORMAT_MACROS
#include <inttypes.h>
//include <stdint.h>
#include <fstream>

extern bool logging_on;

htif_isasim_t::htif_isasim_t(sim_t* _sim, const std::vector<std::string>& args)
  : htif_pthread_t(args), sim(_sim), reset(true), seqno(1), checkpoint(NULL)
{
    checkpointing_active = false;
}

htif_isasim_t::~htif_isasim_t()
{
  if (this->checkpoint) {
    free(this->checkpoint);
  }
}

// This is called by sim as a way to transfer control to HTIF host module so that any pending
// transactions at any point in time can be completed.
bool htif_isasim_t::tick()
{
  ifprintf(logging_on,stderr,"****Ticking HTIF at inst COUNT %lu****\n",sim->get_core(0)->get_state()->count);
  // If simulation complete then say that nothing more to do.
  // done() returns false when the system is resetting or when the simulator has stopped running
  // Returns true at other times
  if (done())
    return false;

  if(reset){
    fprintf(stderr,"****Initializing the processor system****\n");
  }

  // If reset is set as true, which it is during initialization, the HTIF host module sends a bunch
  // of packets to initialize memory and processor state. Keep stepping the HTIF for init sequence 
  // to complete before returning control to the caller. The HTIF host module sets reset to low once 
  // the init sequence is complete.
  // If reset is low (normal operation) tick only once to complete a single pending transaction
  do tick_once(); while (reset);

  return true;
}

void htif_isasim_t::tick_once()
{
  packet_header_t hdr;
  recv(&hdr, sizeof(hdr));

  char buf[hdr.get_packet_size()];
  memcpy(buf, &hdr, sizeof(hdr));
  recv(buf + sizeof(hdr), hdr.get_payload_size());
  packet_t p(buf);

  assert(hdr.seqno == seqno);

  if(reset){
    ifprintf(logging_on,stderr,"Receiving initialization packet seq no: %" PRIu8 "\n",seqno);
  }
  else{
    ifprintf(logging_on,stderr,"Receiving command packet seq no: %" PRIu8 "\n", seqno);
  }


  switch (hdr.cmd)
  {
    case HTIF_CMD_READ_MEM:
    {
      ifprintf(logging_on,stderr,"HTIF_CMD_READ_MEM seq no: %" PRIu8 "\n", seqno);

      packet_header_t ack(HTIF_CMD_ACK, seqno, hdr.data_size, 0);
      send(&ack, sizeof(ack));

      uint64_t buf[hdr.data_size];
      for (size_t i = 0; i < hdr.data_size; i++)
        buf[i] = sim->debug_mmu->load_uint64((hdr.addr+i)*HTIF_DATA_ALIGN);

      if(checkpointing_active){
        *checkpoint << "READ_MEM" << " " << hdr.addr << " " << hdr.data_size << std::endl;
        for (size_t i = 0; i < hdr.data_size; i++){
          *checkpoint << buf[i] << " ";
        }
        *checkpoint << std::endl;
      }

      send(buf, hdr.data_size * sizeof(buf[0]));
      break;
    }
    case HTIF_CMD_WRITE_MEM:
    {
      ifprintf(logging_on,stderr,"HTIF_CMD_WRITE_MEM seq no: %" PRIu8 "\n", seqno);

      const uint64_t* buf = (const uint64_t*)p.get_payload();
      for (size_t i = 0; i < hdr.data_size; i++)
        sim->debug_mmu->store_uint64((hdr.addr+i)*HTIF_DATA_ALIGN, buf[i]);

      if(checkpointing_active){
        *checkpoint << "WRITE_MEM" << " " << hdr.addr << " " << hdr.data_size << std::endl;
        for (size_t i = 0; i < hdr.data_size; i++){
          *checkpoint << buf[i] << " ";
        }
        *checkpoint << std::endl;
      }

      packet_header_t ack(HTIF_CMD_ACK, seqno, 0, 0);
      send(&ack, sizeof(ack));
      break;
    }
    case HTIF_CMD_READ_CONTROL_REG:
    case HTIF_CMD_WRITE_CONTROL_REG:
    {

      assert(hdr.data_size == 1);
      reg_t coreid = hdr.addr >> 20;
      reg_t regno = hdr.addr & ((1<<20)-1);
      uint64_t old_val, new_val = 0 /* shut up gcc */;

      ifprintf(logging_on,stderr,"HTIF_CMD_READ/WRITE_CONTROL_REG reg no: %" PRIreg " seq no: %" PRIu8 "\n",regno, seqno);

      packet_header_t ack(HTIF_CMD_ACK, seqno, 1, 0);
      send(&ack, sizeof(ack));

      if (coreid == 0xFFFFF) // system control register space
      {
        uint64_t scr = sim->get_scr(regno);
        if(checkpointing_active){
          *checkpoint << "MOD_SCR " << coreid << " " << regno << " " << scr << " " << scr << std::endl;
        }
        send(&scr, sizeof(scr));
        break;
      }

      processor_t* proc = sim->get_core(coreid);
      bool write = hdr.cmd == HTIF_CMD_WRITE_CONTROL_REG;
      if (write)
        memcpy(&new_val, p.get_payload(), sizeof(new_val));

      //fprintf(stderr,"HTIF_CMD_READ/WRITE_CONTROL_REG reg no: %" PRIreg " seq no: %" PRIu8 "\n",regno, seqno);

      // TODO mapping HTIF regno to CSR[4:0] is arbitrary; consider alternative
      switch (regno)
      {
        case CSR_HARTID & 0x1f:
          old_val = coreid;
          break;
        case CSR_TOHOST & 0x1f:
          old_val = proc->get_state()->tohost;
          if (write)
            proc->get_state()->tohost = new_val;
          break;
        case CSR_FROMHOST & 0x1f:
          old_val = proc->get_state()->fromhost;
          if (write && old_val == 0)
            proc->set_fromhost(new_val);
          break;
        case CSR_RESET & 0x1f:
          old_val = !proc->running();
          if (write)
          {
            reset = reset & (new_val & 1);
            proc->reset(new_val & 1);
            if(!new_val){
              fprintf(stderr,"****Initialization complete****\n");
            }
          }
          break;
        default:
          abort();
      }

      // Print TOHOST content only when something significant happens)
      if((regno != (CSR_TOHOST & 0x1f)) || ((old_val != 0) || (old_val != new_val))){
        if(checkpointing_active){
          *checkpoint << "MOD_SCR " << coreid << " " << regno << " " << old_val << " " << new_val << std::endl;
        }
      }
      send(&old_val, sizeof(old_val));
      break;
    }
    default:
      abort();
  }
  seqno++;
}

bool htif_isasim_t::done()
{
  if (reset)
    return false;
  return !sim->running();
}

void htif_isasim_t::setup_replay_state(replay_pkt_t *hdr)
{

  //hdr->dump();
  if(hdr->command == READ_MEM)
  {
    for (size_t i = 0; i < hdr->data_size; i++)
      sim->debug_mmu->store_uint64((hdr->addr+i)*HTIF_DATA_ALIGN, hdr->data[i]);
  }
  else  if(hdr->command == MOD_SCR)
  {
    processor_t* proc = sim->get_core(hdr->coreid);
  // TODO mapping HTIF regno to CSR[4:0] is arbitrary; consider alternative
    switch (hdr->regno)
    {
      case CSR_TOHOST & 0x1f:
        proc->get_state()->tohost = hdr->old_regval;
        break;
      case CSR_FROMHOST & 0x1f:
        proc->set_fromhost(hdr->old_regval);
        break;
      default:
        abort();
    }

  }
}

//bool htif_isasim_t::restore_checkpoint(std::string restore_file)
bool htif_isasim_t::restore_checkpoint(std::istream& restore)
{
  if (done())
    return false;

  if(reset){
    fprintf(stderr,"****Initializing the processor system****\n");
  }

  // If reset is set as true, which it is during initialization, the HTIF host module sends a bunch
  // of packets to initialize memory and processor state. Keep stepping the HTIF for init sequence 
  // to complete before returning control to the caller. The HTIF host module sets reset to low once 
  // the init sequence is complete.
  // If reset is low (normal operation) tick only once to complete a single pending transaction
  //do tick_once(); while (reset);

  FILE* restore_log = fopen("restore.htif","w");

  std::string token1;
  reg_t token2, token3;
  replay_pkt_t pkt;
  
  while(restore.good())
  {
    restore >> token1 >> token2 >> token3;
    fprintf(restore_log,"Reading line: %s %ld %ld\n",token1.c_str(),token2,token3);
    if(!token1.compare("READ_MEM"))
    {

      fprintf(restore_log,"In READ_MEM\n");
      // Create the data packet
      pkt.command = READ_MEM;
      pkt.addr = token2;
      pkt.data_size = token3;
      for(unsigned int i=0; i < token3; i++)
        restore >> pkt.data[i];
    } 
    else if(!token1.compare("MOD_SCR"))
    {
      fprintf(restore_log,"In MOD_SCR\n");
      // Update packet with SCR values
      pkt.command = MOD_SCR;
      pkt.coreid = token2;
      pkt.regno = token3;
      restore >> pkt.old_regval;
      restore >> pkt.new_regval;
    } 
    else if(!token1.compare("END_HTIF_CHECKPOINT"))
    {
      // HTIF checkpoint restore complete
      //fprintf(stderr,"Done restoring HTIF state\n");
      //Read in the rest of the line so that correct token is read in the next iteration
      std::string dummy_line;
      std::getline(restore,dummy_line); 
      // Must tick to maintain the sequence of HTIF operations
      tick_once();
      break;
    }
    else
    {
      //Read in the rest of the line so that correct token is read in the next iteration
      std::string dummy_line;
      std::getline(restore,dummy_line); 
      // Must tick to maintain the sequence of HTIF operations
      tick_once();
      continue;
    }
    // Setup the system state and the tick HTIF once
    setup_replay_state(&pkt);
    tick_once();
  }

  fclose(restore_log);

  return true;

}

void htif_isasim_t::start_checkpointing()
{
  checkpointing_active = true;
  this->checkpoint = new std::stringstream();
}

void htif_isasim_t::output_checkpointing(std::ostream& checkpoint_file)
{
  if(checkpointing_active){
	  this->checkpoint->seekg(0);
	  std::copy(
	  	    std::istreambuf_iterator<char>(*this->checkpoint),
	  	    std::istreambuf_iterator<char>(),
	  	    std::ostreambuf_iterator<char>(checkpoint_file)
	  );
	  checkpoint_file << "END_HTIF_CHECKPOINT 0 0 0" << std::endl;
  }

}

void htif_isasim_t::stop_checkpointing()
{
  if(checkpointing_active){
   *checkpoint << "END_HTIF_CHECKPOINT 0 0 0" << std::endl;
  }

  checkpointing_active = false;
}

