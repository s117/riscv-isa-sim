// See LICENSE for license details.

#include "htif.h"
#include "sim.h"
#include "encoding.h"
#include "stream_compression.h"
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
  : htif_pthread_t(args), sim(_sim), reset(true), seqno(1)
{
}

htif_isasim_t::~htif_isasim_t() {}

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

      packet_header_t ack(HTIF_CMD_ACK, seqno++, hdr.data_size, 0);
      send(&ack, sizeof(ack));

      uint64_t buf[hdr.data_size];
      for (size_t i = 0; i < hdr.data_size; i++)
        buf[i] = sim->debug_mmu->load_uint64((hdr.addr+i)*HTIF_DATA_ALIGN);

      send(buf, hdr.data_size * sizeof(buf[0]));
      break;
    }
    case HTIF_CMD_WRITE_MEM:
    {
      ifprintf(logging_on,stderr,"HTIF_CMD_WRITE_MEM seq no: %" PRIu8 "\n", seqno);

      const uint64_t* buf = (const uint64_t*)p.get_payload();
      for (size_t i = 0; i < hdr.data_size; i++)
        sim->debug_mmu->store_uint64((hdr.addr+i)*HTIF_DATA_ALIGN, buf[i]);

      packet_header_t ack(HTIF_CMD_ACK, seqno++, 0, 0);
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

      packet_header_t ack(HTIF_CMD_ACK, seqno++, 1, 0);
      send(&ack, sizeof(ack));

      if (coreid == 0xFFFFF) // system control register space
      {
        uint64_t scr = sim->get_scr(regno);
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
        case CSR_COUNT & 0x1f:
          old_val = proc->get_state()->count;
          break;
        default:
          abort();
      }

      send(&old_val, sizeof(old_val));
      break;
    }
    case HTIF_CMD_DOWNLOAD_HART_FULL_STATE:
    {
      ifprintf(logging_on, stderr, "HTIF_CMD_DOWNLOAD_HART_FULL_STATE seq no: %" PRIu8 "\n", seqno);

      reg_t coreid = hdr.addr;
      const state_t *core_state = sim->get_core(coreid)->get_state();

      size_t ds = (sizeof(state_t) + HTIF_DATA_ALIGN - 1) / HTIF_DATA_ALIGN;
      std::vector<uint8_t> padded_buf(ds * HTIF_DATA_ALIGN, 0);
      memcpy(padded_buf.data(), core_state, sizeof(*core_state));

      packet_header_t ack(HTIF_CMD_ACK, seqno++, ds, sizeof(state_t)); // use addr to pass the actual size of the dumped state
      send(&ack, sizeof(ack));
      send(padded_buf.data(), padded_buf.size());
      break;
    }
    case HTIF_CMD_UPLOAD_HART_FULL_STATE:
    {
      ifprintf(logging_on, stderr, "HTIF_CMD_UPLOAD_HART_FULL_STATE seq no: %" PRIu8 "\n", seqno);

      reg_t coreid = hdr.addr;
      processor_t *proc = sim->get_core(coreid);
      state_t *core_state = proc->get_state();

      // The state must be the padded size of this build's state_t, as sent by HTIF_CMD_DOWNLOAD_HART_FULL_STATE
      const size_t expected_payload_size = (sizeof(state_t) + HTIF_DATA_ALIGN - 1) / HTIF_DATA_ALIGN * HTIF_DATA_ALIGN;
      if (p.get_payload_size() != expected_payload_size)
        throw std::runtime_error(
          "Error happened while loading the full state of HART " + std::to_string(coreid) + ": the host sent " +
          std::to_string(p.get_payload_size()) + " bytes, but this simulator expects " +
          std::to_string(expected_payload_size) + " bytes. The state was probably dumped by a different build.");

      const uint8_t *buf = (const uint8_t *) p.get_payload();
      memcpy(core_state, buf, sizeof(*core_state));
      // Recompute what is derived from the status register (e.g. rv64), and flush the TLB and icache
      // that were filled before the state was loaded.
      proc->set_pcr(CSR_STATUS, core_state->sr);

      packet_header_t ack(HTIF_CMD_ACK, seqno++, 0, sizeof(state_t)); // use addr to pass the actual size of the data used
      send(&ack, sizeof(ack));
      break;
    }
    case HTIF_CMD_READ_HART_EXEC_CONTROL_REG:
    case HTIF_CMD_WRITE_HART_EXEC_CONTROL_REG:
    {
      reg_t coreid = hdr.addr >> 20;
      reg_t regno = hdr.addr & ((1 << 20) - 1);
      reg_t old_val, new_val = 0 /* shut up gcc */;

      ifprintf(
        logging_on, stderr, "HTIF_CMD_%s_CORE_EXEC_CONTROL_REG reg no: %" PRIreg " seq no: %" PRIu8 "\n",
        hdr.cmd == HTIF_CMD_READ_HART_EXEC_CONTROL_REG ? "READ" : "WRITE",
        regno, seqno);

      packet_header_t ack(HTIF_CMD_ACK, seqno++, 1, 0);
      send(&ack, sizeof(ack));
      processor_t *proc = sim->get_core(coreid);

      if (hdr.cmd == HTIF_CMD_WRITE_HART_EXEC_CONTROL_REG)
      {
        assert(hdr.data_size == 1);
        memcpy(&new_val, p.get_payload(), sizeof(new_val));
        old_val = proc->write_exec_ctrl_cr(regno, new_val);
      }
      else
      {
        assert(hdr.data_size == 0);
        old_val = proc->read_exec_ctrl_cr(regno);
      }
      send(&old_val, sizeof(old_val));
      break;
    }
    case HTIF_CMD_DOWNLOAD_MEM_DUMP:
    {
      ifprintf(logging_on, stderr, "HTIF_CMD_DOWNLOAD_MEM_DUMP seq no: %" PRIu8 "\n", seqno);
      size_t total_sent = 0;
      assert(hdr.addr == 0); // Initiating packet must have hdr.addr == 0 [H.1]
      stream_compression_t::compress_region(sim->mem, sim->memsz, chunk_max_size(), [this, &total_sent](const char *data, size_t len) {
        // Send compressed memory stream [T.1]
        size_t ds = (len + HTIF_DATA_ALIGN - 1) / HTIF_DATA_ALIGN;
        packet_header_t ack(HTIF_CMD_ACK, seqno++, ds, len);
        send(&ack, sizeof(ack));
        send(data, ds * HTIF_DATA_ALIGN);
        total_sent += len;

        // Wait for the polling request from host [H.2]
        packet_header_t polling_hdr;
        recv(&polling_hdr, sizeof(polling_hdr));
        assert(polling_hdr.seqno == seqno);
        assert(polling_hdr.get_payload_size() == 0);
        assert(polling_hdr.cmd == HTIF_CMD_DOWNLOAD_MEM_DUMP);
        assert(polling_hdr.addr == len);
      });

      // Send eof-of-stream packet [T.2]
      packet_header_t ack(HTIF_CMD_ACK, seqno++, 0, total_sent);
      send(&ack, sizeof(ack));
      break;
    }
    case HTIF_CMD_UPLOAD_MEM_DUMP:
    {
      ifprintf(logging_on, stderr, "HTIF_CMD_UPLOAD_MEM_DUMP seq no: %" PRIu8 "\n", seqno);
      const size_t receiving_capacity = chunk_max_size();
      // Initiating packet must have hdr.addr == 0 [H.1]
      assert(hdr.addr == 0);
      std::vector<char> recv_buf(receiving_capacity);

      size_t total_received = 0;
      // This initial value is special, as it is for telling the host what's the target's max receiving capacity [T.1]
      size_t prev_received_size = receiving_capacity;
      bool end_of_stream = false;

      size_t inflated_size = stream_compression_t::decompress_region(sim->mem, sim->memsz, [this, &recv_buf, &end_of_stream, &total_received, &prev_received_size](char *&data, size_t &len) {
        assert(!end_of_stream);
        // ACK the host with hdr.addr be the max receiving capacity for the first reply [T.1],
        // then ACK the host with hdr.addr to be the previously received bytes. [T.2]
        packet_header_t ack(HTIF_CMD_ACK, seqno++, 0, prev_received_size);
        send(&ack, sizeof(ack));

        // Receive the streaming packet from host [H.2] / [H.3]
        packet_header_t streaming_hdr;
        recv(&streaming_hdr, sizeof(streaming_hdr));
        if (streaming_hdr.cmd != HTIF_CMD_UPLOAD_MEM_DUMP || streaming_hdr.seqno != seqno)
          throw std::runtime_error(
            "Error happened while load memory dump to target: expected an upload packet with seqno " +
            std::to_string(seqno) + ", but received cmd " + std::to_string(streaming_hdr.cmd) +
            " with seqno " + std::to_string(streaming_hdr.seqno) + ".");

        end_of_stream = streaming_hdr.get_payload_size() == 0;
        if (end_of_stream)
        {
          // End of stream, hdr.addr is the total bytes sent, counted at the host side [H.3]
          if (streaming_hdr.addr != total_received)
            throw std::runtime_error(
              "Error happened while load memory dump to target: the host reported that " +
              std::to_string(streaming_hdr.addr) +
              " bytes was sent to target, but target only received " +
              std::to_string(total_received) +
              " bytes.");
          data = nullptr;
          len = 0;
          return;
        }
        // Received a streamed chunk [H.2]
        // Checked at runtime (not with assert) as they guard the receiving buffer
        if (streaming_hdr.get_payload_size() > recv_buf.size())
          throw std::runtime_error(
            "Error happened while load memory dump to target: the host sent a " +
            std::to_string(streaming_hdr.get_payload_size()) + " bytes chunk, but the target can only receive " +
            std::to_string(recv_buf.size()) + " bytes at a time.");
        recv(recv_buf.data(), streaming_hdr.get_payload_size());
        if (streaming_hdr.addr > streaming_hdr.get_payload_size())
          throw std::runtime_error(
            "Error happened while load memory dump to target: the host reported " +
            std::to_string(streaming_hdr.addr) + " effective bytes in a " +
            std::to_string(streaming_hdr.get_payload_size()) + " bytes chunk.");
        total_received += streaming_hdr.addr;

        // Feed it to the decompressor
        data = recv_buf.data();
        len = streaming_hdr.addr;
        prev_received_size = streaming_hdr.addr;
      });

      // The memory has been replaced, drop what the cores have translated and decoded from the old content
      for (size_t i = 0; i < sim->num_cores(); i++)
        sim->get_core(i)->get_mmu()->flush_tlb();

      // Confirm the decompressed data size to host
      packet_header_t ack(HTIF_CMD_ACK, seqno++, 0, inflated_size);
      send(&ack, sizeof(ack));
      break;
    }
    default:
      abort();
  }
}

bool htif_isasim_t::done()
{
  if (reset)
    return false;
  return !sim->running();
}
