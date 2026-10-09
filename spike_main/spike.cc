// See LICENSE for license details.

#include "sim.h"
#include "htif.h"
#include "cachesim.h"
#include "extension.h"
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
  fprintf(stderr, "  -h                 Print this help message\n");
  fprintf(stderr, "  -p<n>              Simulate <n> processors\n");
  fprintf(stderr, "  -m<n>              Provide <n> MB of target memory\n");
  fprintf(stderr, "  -d                 Interactive debug mode\n");
  fprintf(stderr, "  -g                 Track histogram of PCs\n");
  fprintf(stderr, "  -s<interval>       Dump basic block vector profile for Simpoint with specified interval\n");
  fprintf(stderr, "  -e<n>              Stop the simulation once executed <n> instructions.\n");
  fprintf(stderr, "\n");
  fprintf(stderr, "  --ic=<S>:<W>:<B>   Instantiate a cache model with S sets,\n");
  fprintf(stderr, "  --dc=<S>:<W>:<B>     W ways, and B-byte blocks (with S and\n");
  fprintf(stderr, "  --l2=<S>:<W>:<B>     B both powers of 2).\n");
  fprintf(stderr, "  --extension=<name> Specify RoCC Extension\n");
  fprintf(stderr, "  --extlib=<name>    Shared library to load\n");
  fprintf(stderr, "\n");
  fprintf(stderr, "Checkpoints are created and loaded by the FESVR, see its options\n");
  fprintf(stderr, "  +create-checkpoint=<ckpt-desc-file> and +load-checkpoint=<ckpt-file>,\n");
  fprintf(stderr, "  both of which require +dev-traffic-replay=<recording>.\n");
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
  size_t nprocs = 1;
  size_t mem_mb = 0;
  std::unique_ptr<icache_sim_t> ic;
  std::unique_ptr<dcache_sim_t> dc;
  std::unique_ptr<cache_sim_t> l2;
  std::function<extension_t*()> extension;

  uint64_t stop_amt           = NO_STOP;

  option_parser_t parser;
  parser.help(&help);
  parser.option('h', 0, 0, [&](const char* s){help();});
  parser.option('d', 0, 0, [&](const char* s){debug = true;});
  parser.option('g', 0, 0, [&](const char* s){histogram = true;});
  parser.option('s', 0, 1, [&](const char* s){simpoint = true; simpoint_interval = atol(s);});
  parser.option('p', 0, 1, [&](const char* s){nprocs = atoi(s);});
  parser.option('m', 0, 1, [&](const char* s){mem_mb = atoi(s);});
  parser.option('e', 0, 1, [&](const char* s){stop_amt = atoll(s);});
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

  int htif_code = true;

  // Initialize the processor
  s.boot();

  if (stop_amt == NO_STOP) {
    htif_code = s.run();
  } else {
    htif_code = s.run(stop_amt);
  }

  return htif_code;
}
