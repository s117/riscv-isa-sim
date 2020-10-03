#include <cinttypes>
#include <string>
#include <cstring>
#include "gzstream.h"
#include "bbtracker.h"

#define __FREQ_VEC_ELEMENT_T_concat(size) uint##size##_t
#define __FREQ_VEC_ELEMENT_T_expansion(s) __FREQ_VEC_ELEMENT_T_concat(s)

#define PC_SAMPLING_BIT 13u
#define PC_SAMPLING_MASK ((1u<<PC_SAMPLING_BIT)-1u)
#define FREQ_VEC_SIZE (1u<<PC_SAMPLING_BIT)


#define FREQ_VEC_ELEMENT_SIZE 32
#define FREQ_VEC_ELEMENT_T __FREQ_VEC_ELEMENT_T_expansion(FREQ_VEC_ELEMENT_SIZE)

#if (bb_size >= (1 << FREQ_VEC_ELEMENT_SIZE - 1))
#warning "PC Frequency vector array element does not has enough bit for all the possible value"
#endif

// Index is PC=PC>>2, PC[ 2*SAMPLING_BIT-1 : SAMPLING_BIT ] xor PC[ SAMPLING_BIT-1 : 0 ]
#define GET_FREQ_VEC_POS_BY_PC(pc) ( \
      (((pc >> 2u) >> PC_SAMPLING_BIT) & (PC_SAMPLING_MASK)) ^ \
      (((pc >> 2u) & PC_SAMPLING_MASK)) \
    )


class pc_freqvec_tracker_t {

private:
  FREQ_VEC_ELEMENT_T freqvec[FREQ_VEC_SIZE] = {0};
  FREQ_VEC_ELEMENT_T insn_in_vec = 0;
  ogzstream freqvec_out;

  void reset_vec() {
    insn_in_vec = 0;
    memset(freqvec, 0, sizeof(freqvec));
  }

public:
  pc_freqvec_tracker_t() = default;

  ~pc_freqvec_tracker_t() {
    if (insn_in_vec) finish_vec();
    freqvec_out.close();
  };

  void init_pc_freqvec_tracker(const char *dir_name, const char *out_name) {
    reset_vec();

    std::string finalname = std::string(dir_name) + "/" + out_name + ".pcfreq.gz";
    freqvec_out.open(finalname.c_str());
    if (!freqvec_out.good()) {
      std::cerr << "PC Frequency Vector output error: fail to open output file" << finalname << std::endl;
      exit(1);
    }
  }

  void update_vec(uint64_t pc) {
    ++insn_in_vec;
    ++freqvec[GET_FREQ_VEC_POS_BY_PC(pc)];
  };

  void finish_vec() {
    freqvec_out << insn_in_vec << " : ";
    for (auto &i: freqvec) {
      freqvec_out << i << " ";
    }
    freqvec_out << std::endl;

    reset_vec();
  };
};
