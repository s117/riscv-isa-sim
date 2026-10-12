// See LICENSE for license details.

#ifndef _RISCV_HART_STATE_CODEC_H
#define _RISCV_HART_STATE_CODEC_H

#include "processor.h"
#include <cstddef>
#include <cstdint>
#include <vector>

// Conversion of a HART's architectural state (state_t) to and from the portable format defined in
// fesvr/hart_state_format.h, as exchanged with the host and saved in checkpoints.

/**
 * Encode the state, in the portable format.
 */
std::vector<uint8_t> encode_hart_state(const state_t &state);

/**
 * Decode a state in the portable format into state. Every field of state_t must have an entry, and every entry must
 * name a field of state_t.
 * @throw std::runtime_error Why data can't be decoded (a phrase starting with "it"); state is left unchanged then.
 */
void decode_hart_state(const uint8_t *data, size_t size, state_t &state);

#endif
