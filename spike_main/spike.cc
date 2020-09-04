// See LICENSE for license details.

#include "sim.h"
#include "htif.h"
#include "cachesim.h"
#include "extension.h"
#include "ckpt_desc_reader.h"
#include <dlfcn.h>
#include <fesvr/option_parser.h>
#include <stdio.h>
#include <stdlib.h>
#include <getopt.h>
#include <vector>
#include <string>
#include <memory>

#define NO_STOP UINT64_MAX

static void help()
{
  fprintf(stderr, "usage: spike [host options] <target program> [target options]\n");
  fprintf(stderr, "Host Options:\n");
  fprintf(stderr, "  -p<n>              Simulate <n> processors\n");
  fprintf(stderr, "  -m<n>              Provide <n> MB of target memory\n");
  fprintf(stderr, "  -d                 Interactive debug mode\n");
  fprintf(stderr, "  -g                 Track histogram of PCs\n");
  fprintf(stderr, "  -s <Interval>      Dump basic block vector profile for Simpoint with specified interval\n");
  fprintf(stderr, "  -t<n> / -t<s>,<n>    Trace the simulation to file trace_proc_[coreid].gz\n");
  fprintf(stderr, "                       If <s> is given, will skip <s> instructions prior to tracing\n");
  fprintf(stderr, "                       If <n> is 0 the entire trace will be kept, otherwise only keep\n");
  fprintf(stderr, "                       the trace of last <n> instruction before simulation stop.\n");
  fprintf(stderr, "  -h                 Print this help message\n");
  fprintf(stderr, "  --ic=<S>:<W>:<B>   Instantiate a cache model with S sets,\n");
  fprintf(stderr, "  --dc=<S>:<W>:<B>     W ways, and B-byte blocks (with S and\n");
  fprintf(stderr, "  --l2=<S>:<W>:<B>     B both powers of 2).\n");
  fprintf(stderr, "  --extension=<name> Specify RoCC Extension\n");
  fprintf(stderr, "  --extlib=<name>    Shared library to load\n");
  exit(1);
}

long long str2ll(const char* s) {
  char *end;
  if (s[0] == '\0'){
    throw std::runtime_error(std::string("cannot parse empty str to number"));
  }

  long long val = std::strtoll(s, &end, 10);

  if (*end != '\0') {
    throw std::runtime_error("failed to parse "+ std::string(s) + " to number");
  }
  return val;
}

int main(int argc, char** argv)
{
  bool debug = false;
  bool histogram = false;
  bool simpoint = false;
  size_t simpoint_interval = 100000000;
  bool checkpoint = false;
  size_t checkpoint_skip_amt = 0;
  size_t nprocs = 1;
  size_t mem_mb = 0;
  std::unique_ptr<icache_sim_t> ic;
  std::unique_ptr<dcache_sim_t> dc;
  std::unique_ptr<cache_sim_t> l2;
  std::function<extension_t*()> extension;

  bool trace = false;
  size_t trace_skip_amt = 0;
  size_t trace_last_n = 0;

  uint64_t stop_amt           = NO_STOP;
  std::string checkpoint_file = "";
  std::string checkpoint_desc_file = "";

  option_parser_t parser;
  parser.help(&help);
  parser.option('h', 0, 0, [&](const char* s){help();});
  parser.option('d', 0, 0, [&](const char* s){debug = true;});
  parser.option('g', 0, 0, [&](const char* s){histogram = true;});
  parser.option('s', 0, 1, [&](const char* s){simpoint = true; simpoint_interval = atol(s);});
  parser.option('p', 0, 1, [&](const char* s){nprocs = atoi(s);});
  parser.option('m', 0, 1, [&](const char* s){mem_mb = atoi(s);});
  parser.option('e', 0, 1, [&](const char* s){stop_amt = atoll(s);});
  parser.option('c', 0, 1, [&](const char* s){checkpoint = true; checkpoint_desc_file = s;});
  parser.option('f', 0, 1, [&](const char* s){checkpoint_file = s;});
  parser.option('t', 0, 1, [&](const char* s){
    trace = true;
    std::string param(s);
    auto delimiter_pos = param.find(',');
    if (delimiter_pos == std::string::npos) {
      trace_skip_amt = 0;
      trace_last_n = str2ll(param.c_str());
    } else{
      std::string p1 = param.substr(0, delimiter_pos);
      std::string p2 = param.substr(delimiter_pos + 1, param.size());
      trace_skip_amt = str2ll(p1.c_str());
      trace_last_n = str2ll(p2.c_str());
    }
  });
  parser.option(0, "ic", 1, [&](const char* s){ic.reset(new icache_sim_t(s));});
  parser.option(0, "dc", 1, [&](const char* s){dc.reset(new dcache_sim_t(s));});
  parser.option(0, "l2", 1, [&](const char* s){l2.reset(cache_sim_t::construct(s, "L2$"));});
  parser.option(0, "extension", 1, [&](const char* s){extension = find_extension(s);});
  parser.option(0, "extlib", 1, [&](const char *s){
    void *lib = dlopen(s, RTLD_NOW | RTLD_GLOBAL);
    if (lib == NULL) {
      fprintf(stderr, "Unable to load extlib '%s': %s\n", s, dlerror());
      exit(-1);
    }
  });

  auto argv1 = parser.parse(argv);
  if (!*argv1)
    help();
  std::vector<std::string> htif_args(argv1, (const char*const*)argv + argc);
  sim_t s(nprocs, mem_mb, htif_args);

  if (ic && l2) ic->set_miss_handler(&*l2);
  if (dc && l2) dc->set_miss_handler(&*l2);
  for (size_t i = 0; i < nprocs; i++)
  {
    if (ic) s.get_core(i)->get_mmu()->register_memtracer(&*ic);
    if (dc) s.get_core(i)->get_mmu()->register_memtracer(&*dc);
    if (extension) s.get_core(i)->register_extension(extension());
  }

  s.set_debug(debug);
  s.set_histogram(histogram);

#ifdef RISCV_ENABLE_SIMPOINT
  s.set_simpoint(simpoint, simpoint_interval);
#else
  if(simpoint){
    fprintf(stderr, "Spike wasn't compiled with Simpoint support.\n");
    exit(-1);
  }
#endif

#ifndef RISCV_ENABLE_DBG_TRACE
  if(trace) {
    fprintf(stderr, "Spike wasn't compiled with tracing support.\n");
    exit(-1);
  }
#else
  if (trace && checkpoint) {
    fprintf(stderr, "Doesn't support tracing and checkpointing together.\n");
    exit(-1);
  }
#endif

  int htif_code = true;

  if(checkpoint && (checkpoint_file == ""))
  {
    checkpoint_file = "checkpoint_"+std::to_string(checkpoint_skip_amt);
  }

  // Initialize the processor before dumping/restoring checkpoint
  s.boot();

  if (checkpoint) { // Runs Spike in checkpoint mode
    s.init_checkpoint();
    // Load the checkpoint description
    ckpt_desc_list ckpt_descs;
    try {
      ckpt_descs = ckpt_desc_file_read(std::string(checkpoint_desc_file));
      ckpt_desc_print(ckpt_descs);
    } catch (std::runtime_error &ex) {
      std::cout << "Fail to load checkpoint description file, reason:\n" << ex.what() << std::endl;
      exit(-1);
    }
    size_t amt_ran = 0;

    for (auto &it : ckpt_descs) {
      size_t step = it.second - amt_ran;
      fprintf(stderr, "Skipping for %lu instructions before next checkpointing\n",step);
      htif_code = s.run(step);
      // Stop simulation if HTIF returns false
      if(!htif_code) return htif_code;

      fprintf(stderr, "Creating Checkpoint\n");
      htif_code = s.create_checkpoint(it.first);
      // Stop simulation if HTIF returns false
      if(!htif_code){
        fprintf(stderr, "Checkpoint Creation Failed: HTIF Exit Code %d\n",htif_code);
        return htif_code;
      }

      amt_ran += step;
    }
  } else { // Run Spike in normal mode
    if (!checkpoint_file.empty()) { // Starting from a checkpoint?
      fprintf(stderr, "Restoring checkpoint from %s\n",checkpoint_file.c_str());
      s.restore_checkpoint(checkpoint_file);
    }

#ifdef RISCV_ENABLE_DBG_TRACE
    if (trace) { // trace enabled?
      if (trace_skip_amt) {
        if (stop_amt != NO_STOP) {
          if (trace_skip_amt >= stop_amt) {
            fprintf(stderr, "Error: trace skip amount must smaller than stop amount\n");
            exit(-1);
          }
          stop_amt -= trace_skip_amt;
        }
        fprintf(stderr, "Fast forwarding %lu instruction before start tracing...\n", trace_skip_amt);
        htif_code = s.run(trace_skip_amt);
        fprintf(stderr, "Start tracing...\n");
      }
      if (htif_code) {
        s.enable_trace(trace_last_n);
      } else {
        fprintf(stderr, "Warning: program ended before tracer is engaged\n");
        return htif_code;
      }
    }
#endif
    if (stop_amt == NO_STOP) {
      htif_code = s.run();
    } else {
      htif_code = s.run(stop_amt);
    }
  }

  return htif_code;
}
