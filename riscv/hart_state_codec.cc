// See LICENSE for license details.

#include "hart_state_codec.h"
#include "encoding.h"
#include "fesvr/hart_state_format.h"
#include <cinttypes>
#include <cstdio>
#include <map>
#include <stdexcept>
#include <string>

namespace hsf = hart_state_format;

// Whole entries only, so that a state never needs padding to HTIF_DATA_ALIGN
static_assert(hsf::HEADER_SIZE % 8 == 0 && hsf::ENTRY_SIZE % 8 == 0, "a hart state must be a multiple of 8 bytes");

namespace
{
  // The fields of state_t besides the register files, each with the id of its entry. Calls v(id, field), where field is
  // a reg_t or a uint32_t. This list is the single mapping between state_t and the format, used both ways.
  template <typename State, typename Visitor>
  void visit_scalar_fields(State &s, Visitor &v)
  {
    v(CSR_FFLAGS, s.fflags);
    v(CSR_FRM, s.frm);
    v(CSR_SUP0, s.pcr_k0);
    v(CSR_SUP1, s.pcr_k1);
    v(CSR_EPC, s.epc);
    v(CSR_BADVADDR, s.badvaddr);
    v(CSR_PTBR, s.ptbr);
    v(CSR_COUNT, s.count);
    v(CSR_COMPARE, s.compare);
    v(CSR_EVEC, s.evec);
    v(CSR_CAUSE, s.cause);
    v(CSR_STATUS, s.sr);
    v(CSR_TOHOST, s.tohost);
    v(CSR_FROMHOST, s.fromhost);
    v(hsf::ID_PC, s.pc);
    v(hsf::ID_LOAD_RESERVATION, s.load_reservation);
  }

  std::string id_name(uint32_t id)
  {
    char name[32];
    if (id >= hsf::ID_XPR_BASE && id < hsf::ID_XPR_BASE + NXPR)
      snprintf(name, sizeof(name), "x%u", unsigned(id - hsf::ID_XPR_BASE));
    else if (id >= hsf::ID_FPR_BASE && id < hsf::ID_FPR_BASE + NFPR)
      snprintf(name, sizeof(name), "f%u", unsigned(id - hsf::ID_FPR_BASE));
    else if (id == hsf::ID_PC)
      snprintf(name, sizeof(name), "pc");
    else if (id == hsf::ID_LOAD_RESERVATION)
      snprintf(name, sizeof(name), "load_reservation");
    else if (id >= hsf::ID_CSR_BASE && id < hsf::ID_CSR_END)
      snprintf(name, sizeof(name), "CSR 0x%03x", unsigned(id));
    else
      snprintf(name, sizeof(name), "id 0x%x", unsigned(id));
    return name;
  }

  struct encoder_t
  {
    std::vector<hsf::entry_t> entries;

    template <typename T>
    void operator()(uint32_t id, const T &field)
    {
      entries.push_back({id, uint64_t(field)});
    }
  };

  struct decoder_t
  {
    std::map<uint32_t, uint64_t> values; // entries not consumed yet

    uint64_t take(uint32_t id)
    {
      auto it = values.find(id);
      if (it == values.end())
        throw std::runtime_error("it has no entry for " + id_name(id));
      uint64_t value = it->second;
      values.erase(it);
      return value;
    }

    template <typename T>
    void operator()(uint32_t id, T &field)
    {
      uint64_t value = take(id);
      if (uint64_t(T(value)) != value)
        throw std::runtime_error("its value for " + id_name(id) + " doesn't fit in " + std::to_string(8 * sizeof(T)) + " bits");
      field = T(value);
    }
  };
} // namespace

std::vector<uint8_t> encode_hart_state(const state_t &state)
{
  encoder_t encoder;
  visit_scalar_fields(state, encoder);
  for (uint32_t i = 0; i < NXPR; i++)
    encoder.entries.push_back({hsf::ID_XPR_BASE + i, state.XPR[i]});
  for (uint32_t i = 0; i < NFPR; i++)
    encoder.entries.push_back({hsf::ID_FPR_BASE + i, state.FPR[i]});

  const uint32_t count = encoder.entries.size();
  std::vector<uint8_t> data(hsf::size(count));
  hsf::put_header(data.data(), count);
  for (uint32_t i = 0; i < count; i++)
    hsf::put_entry(data.data(), i, encoder.entries[i]);
  return data;
}

void decode_hart_state(const uint8_t *data, size_t size, state_t &state)
{
  uint32_t count;
  std::string why;
  if (!hsf::check_header(data, size, count, why))
    throw std::runtime_error(why);

  decoder_t decoder;
  for (uint32_t i = 0; i < count; i++)
  {
    const hsf::entry_t entry = hsf::get_entry(data, i);
    if (!decoder.values.emplace(entry.id, entry.value).second)
      throw std::runtime_error("it has more than one entry for " + id_name(entry.id));
  }

  // Decode into a copy, so that state is left unchanged when the data is rejected
  state_t decoded = state;
  visit_scalar_fields(decoded, decoder);
  for (uint32_t i = 0; i < NXPR; i++)
  {
    const uint64_t value = decoder.take(hsf::ID_XPR_BASE + i);
    if (i == 0 && value != 0)
      throw std::runtime_error("its value for x0 isn't 0");
    decoded.XPR.write(i, value);
  }
  for (uint32_t i = 0; i < NFPR; i++)
    decoded.FPR.write(i, decoder.take(hsf::ID_FPR_BASE + i));
  if (!decoder.values.empty())
    throw std::runtime_error("it has an entry for " + id_name(decoder.values.begin()->first) +
                             ", which this simulator doesn't have");

#ifdef RISCV_ENABLE_COMMITLOG
  // Not part of the state: what the last instruction wrote, kept for the commit log
  decoded.log_reg_write.addr = 0;
#endif
  state = decoded;
}
