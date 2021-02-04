//
// Created by s117 on 1/20/21.
//

#include <fstream>
#include <vector>
#include "reconv_predictor.h"

static std::string &ltrim(std::string &str, const std::string &chars = "\t\n\v\f\r ") {
  str.erase(0, str.find_first_not_of(chars));
  return str;
}

static std::string &rtrim(std::string &str, const std::string &chars = "\t\n\v\f\r ") {
  str.erase(str.find_last_not_of(chars) + 1);
  return str;
}

static std::string &trim(std::string &str, const std::string &chars = "\t\n\v\f\r ") {
  return ltrim(rtrim(str, chars), chars);
}

static std::pair<std::string, std::string> rsplit(std::string &s, char p) {
  auto pos = s.find_last_of(p);
  auto l = s.substr(0, pos);
  auto r = s.substr(pos + 1, s.size());
  return std::make_pair(l, r);
}

void parseCSVLine(const std::string &line, std::vector<std::string> *output_vec) {
  std::istringstream line_stream(line);
  std::string tmp;
  output_vec->clear();

  while (getline(line_stream, tmp, ',')) {
    output_vec->push_back(trim(tmp));
  }
}

void RPT_entry::initialize(uint64_t br_pc) {
  BranchPC = br_pc;
  LastBranchTaken = false;
  BelowPotentialActive = false;
  AbovePotentialActive = false;
  ReboundPotentialActive = false;
  BelowPotentialReached = false;
  AbovePotentialReached = false;
  ReboundPotentialReached = false;
  CntActivate = 0;
  CntEarlyDeactivate = 0;
  initialize_BelowPotential();
  initialize_AbovePotential();
  initialize_ReboundPotential();
}

uint8_t RPT_entry::increase_call_level() {
  // Dump the active bits of current call level for future recovery
  uint8_t curr_status = dump_status_bits();

  // Clear all the active bit to avoid inadvertently updating the AR bit
  BelowPotentialActive = false;
  AbovePotentialActive = false;
  ReboundPotentialActive = false;
  BelowPotentialReached = false;
  AbovePotentialReached = false;
  ReboundPotentialReached = false;

  return curr_status;
}

void RPT_entry::decrease_call_level(uint8_t old_active_bits) {
  // Update the HitReturn bit
  if (BelowPotentialActive) {
    BelowPotential.set_HitReturn();
    // TODO: Justify this with the observation from above_test
    if (LastBranchTaken) BelowPotential.clear_ARTaken();
    else BelowPotential.clear_ARNTaken();
  }
  if (AbovePotentialActive) {
    AbovePotential.set_HitReturn();
    // TODO: Justify this with the observation from above_test
    if (LastBranchTaken) AbovePotential.clear_ARTaken();
    else AbovePotential.clear_ARNTaken();
  }
  if (ReboundPotentialActive) {
    ReboundPotential.set_HitReturn();
    // TODO: Justify this with the observation from above_test
    if (LastBranchTaken) ReboundPotential.clear_ARTaken();
    else ReboundPotential.clear_ARNTaken();
  }

  // Recover the active bits for the old call level
  restore_status_bits(old_active_bits);
}

// Active the entry for training
void RPT_entry::activate_entry(bool br_taken) {
  // Update the AR bit
  if (BelowPotentialActive) {
    if (LastBranchTaken) BelowPotential.clear_ARTaken();
    else BelowPotential.clear_ARNTaken();
  }
  if (AbovePotentialActive) {
    if (LastBranchTaken) AbovePotential.clear_ARTaken();
    else AbovePotential.clear_ARNTaken();
  }
  if (ReboundPotentialActive) {
    if (LastBranchTaken) ReboundPotential.clear_ARTaken();
    else ReboundPotential.clear_ARNTaken();
  }

  LastBranchTaken = br_taken;
  BelowPotentialActive = true;
  AbovePotentialActive = true;
  ReboundPotentialActive = true;
  BelowPotentialReached = false;
  AbovePotentialReached = false;
  ReboundPotentialReached = false;

  ++CntActivate;
}

// Train the three potentials in this entry
void RPT_entry::train_entry(uint64_t commit_pc) {
  /*
   * When a branch is executed for the first time, initialize the BelowPotential to the sequentially next PC
   * following the branch.
   *
   * If the BelowPotential PC is committed, the BelowPotential becomes inactive.
   *
   * Else if a PC below the BelowPotential is committed, update the BelowPotential to the committed PC,
   * and the BelowPotential becomes inactive.
   */
  if (BelowPotentialActive) {
    if (BelowPotential.get_potential_pc() == commit_pc) reach_BelowPotential();
    else if (BelowPotential.get_potential_pc() < commit_pc) update_BelowPotential(commit_pc);
  }

  /*
   * The AbovePotential is trained similarly to the BelowPotential, except it is only updated in response to
   * PCs committed which are above the targeted branch and below the current AbovePotential value.
   *
   * When a branch is first executed, the AbovePotential is initialized to an invalid value, and will be updated
   * by the PC of the first instruction executed above the targeted branch.
   */
  if (AbovePotentialActive && (commit_pc < BranchPC)) {
    if (!AbovePotential.test_potential_pc_valid() || (AbovePotential.get_potential_pc() < commit_pc))
      update_AbovePotential(commit_pc);
    else if (AbovePotential.get_potential_pc() == commit_pc) reach_AbovePotential();
  }

  /*
   *  If the ReboundPotential is ever executed (before or after the BelowPotential), it becomes inactive.
   *  Else, after the BelowPotential has been executed, observe for the execution of any PC:
   *   - below the targeted branch
   *   - below the ReboundPotential
   *   - above the BelowPotential.
   *  If such a PC is committed, update the ReboundPotential, and the ReboundPotential becomes inactive
   */
  if (ReboundPotentialActive) {
    if (ReboundPotential.get_potential_pc() == commit_pc) reach_ReboundPotential();
    else if (
      BelowPotentialReached &&
      (BranchPC < commit_pc) &&
      (ReboundPotential.get_potential_pc() < commit_pc) &&
      (commit_pc < BelowPotential.get_potential_pc())
      ) {
      update_ReboundPotential(commit_pc);
    }
  }
}

// Call this function only when you want to terminate the current training
void RPT_entry::early_deactivate_entry() {
  BelowPotentialActive = false;
  AbovePotentialActive = false;
  ReboundPotentialActive = false;
  ++CntEarlyDeactivate;
}

uint64_t RPT_entry::make_prediction(RPT_entry::prediction_details *prediction_details) const {
  uint64_t prediction;
  int reason_id, cat_id;
  do {
    // (1) If HitReturn is set for all potential reconvergence PCs, predict the function return.
    reason_id = 1;
    if (BelowPotential.test_HitReturn() && AbovePotential.test_HitReturn() && ReboundPotential.test_HitReturn()) {
      // Use invalid PC-1 to represent return address for now
      cat_id = 0;
      prediction = RECONV_POINT_RETURN;
      break;
    }

    // (2) If some reconvergence PC has its reached first flag set, predict that reconvergence PC.
    reason_id = 2;
    if (BelowPotential.test_ReachedFirst()) {
      cat_id = 1;
      prediction = BelowPotential.get_potential_pc();
      break;
    } else if (AbovePotential.test_ReachedFirst()) {
      cat_id = 2;
      prediction = AbovePotential.get_potential_pc();
      break;
    } else if (ReboundPotential.test_ReachedFirst()) {
      cat_id = 3;
      prediction = ReboundPotential.get_potential_pc();
      break;
    }

    // (3) If some reconvergence PC is always reached whether the branch is taken or not taken, predict. that reconvergence PC.
    reason_id = 3;
    if (BelowPotential.test_ARTaken() && BelowPotential.test_ARNTaken()) {
      cat_id = 1;
      prediction = BelowPotential.get_potential_pc();
      break;
    } else if (AbovePotential.test_ARTaken() && AbovePotential.test_ARNTaken()) {
      cat_id = 2;
      prediction = AbovePotential.get_potential_pc();
      break;
    } else if (ReboundPotential.test_ARTaken() && ReboundPotential.test_ARNTaken()) {
      cat_id = 3;
      prediction = ReboundPotential.get_potential_pc();
      break;
    }

    // (4) If some reconvergence PC is always reached when the branch is either taken or else always reached when the branch is not taken, predict that reconvergence PC.
    reason_id = 4;
    if (BelowPotential.test_ARTaken() || BelowPotential.test_ARNTaken()) {
      cat_id = 1;
      prediction = BelowPotential.get_potential_pc();
      break;
    } else if (AbovePotential.test_ARTaken() || AbovePotential.test_ARNTaken()) {
      cat_id = 2;
      prediction = AbovePotential.get_potential_pc();
      break;
    } else if (ReboundPotential.test_ARTaken() || ReboundPotential.test_ARNTaken()) {
      cat_id = 3;
      prediction = ReboundPotential.get_potential_pc();
      break;
    }

    reason_id = 5;
    // (5) Predict the BelowPotential.
    cat_id = 1;
    prediction = BelowPotential.get_potential_pc();
  } while (false);

  if (prediction_details) {
    prediction_details->cat_id = cat_id;
    prediction_details->reason_id = reason_id;
  }

  return prediction;
}

uint8_t RPT_entry::dump_status_bits() const {
  int
    bp_reached = BelowPotentialReached,
    ap_reached = AbovePotentialReached,
    rp_reached = ReboundPotentialReached,
    bp_active = BelowPotentialActive,
    ap_active = AbovePotentialActive,
    rp_active = ReboundPotentialActive,
    last_taken = LastBranchTaken;

  uint8_t bits = (
    (bp_reached << 6) |
    (ap_reached << 5) |
    (rp_reached << 4) |
    (bp_active << 3) |
    (ap_active << 2) |
    (rp_active << 1) |
    (last_taken << 0)
  );
  return bits;
}

void RPT_entry::restore_status_bits(uint8_t bits) {
  BelowPotentialReached = bits & (1 << 6);
  AbovePotentialReached = bits & (1 << 5);
  ReboundPotentialReached = bits & (1 << 4);
  BelowPotentialActive = bits & (1 << 3);
  AbovePotentialActive = bits & (1 << 2);
  ReboundPotentialActive = bits & (1 << 1);
  LastBranchTaken = bits & (1 << 0);
}

void RPT_entry::initialize_BelowPotential() {
  BelowPotential.set_potential_pc(pc_next(BranchPC));

  BelowPotential.clear_HitReturn();
  BelowPotential.set_ARTaken();
  BelowPotential.set_ARNTaken();
  BelowPotential.set_ReachedFirst();
}

void RPT_entry::initialize_AbovePotential() {
  AbovePotential.invalidate_potential_pc();

  AbovePotential.clear_HitReturn();
  AbovePotential.set_ARTaken();
  AbovePotential.set_ARNTaken();
  AbovePotential.set_ReachedFirst();
}

void RPT_entry::initialize_ReboundPotential() {
  ReboundPotential.set_potential_pc(pc_next(BranchPC));

  ReboundPotential.clear_HitReturn();
  ReboundPotential.set_ARTaken();
  ReboundPotential.set_ARNTaken();
  ReboundPotential.set_ReachedFirst();
}

void RPT_entry::set_all_ReachFirst() {
  BelowPotential.set_ReachedFirst();
  AbovePotential.set_ReachedFirst();
  ReboundPotential.set_ReachedFirst();
}

void RPT_entry::reach_BelowPotential() {
  // Update self's and other's flags
  BelowPotentialActive = false;
  BelowPotentialReached = true;
  if (!AbovePotentialReached && !ReboundPotentialReached) {
    AbovePotential.clear_ReachedFirst();
    ReboundPotential.clear_ReachedFirst();
  }
}

void RPT_entry::reach_AbovePotential() {
  // Update self's and other's flags
  AbovePotentialActive = false;
  AbovePotentialReached = true;
  if (!BelowPotentialReached && !ReboundPotentialReached) {
    BelowPotential.clear_ReachedFirst();
    ReboundPotential.clear_ReachedFirst();
  }
}

void RPT_entry::reach_ReboundPotential() {
  // Update self's and other's flags
  ReboundPotentialActive = false;
  ReboundPotentialReached = true;
  if (!BelowPotentialReached && !AbovePotentialReached) {
    BelowPotential.clear_ReachedFirst();
    AbovePotential.clear_ReachedFirst();
  }
}

void RPT_entry::update_BelowPotential(uint64_t new_potential_pc) {
  // Update Potential Reconvergence Point
  BelowPotential.set_potential_pc(new_potential_pc);
  // Each time after the BelowPotential is updated, the ReboundPotential is given
  // the value of the static instruction following the branch.
  ReboundPotential.set_potential_pc(pc_next(BranchPC));
  // Update self's and other's flags
  BelowPotential.clear_HitReturn();
  BelowPotential.set_ARNTaken();
  BelowPotential.set_ARTaken();
  set_all_ReachFirst();
  BelowPotentialActive = false;
}

void RPT_entry::update_AbovePotential(uint64_t new_potential_pc) {
  // Update Potential Reconvergence Point
  AbovePotential.set_potential_pc(new_potential_pc);
  // Update self's and other's flags
  AbovePotential.clear_HitReturn();
  AbovePotential.set_ARNTaken();
  AbovePotential.set_ARTaken();
  set_all_ReachFirst();
  AbovePotentialActive = false;
}

void RPT_entry::update_ReboundPotential(uint64_t new_potential_pc) {
  // Update Potential Reconvergence Point
  ReboundPotential.set_potential_pc(new_potential_pc);
  // Update self's and other's flags
  ReboundPotential.clear_HitReturn();
  ReboundPotential.set_ARNTaken();
  ReboundPotential.set_ARTaken();
  set_all_ReachFirst();
  ReboundPotentialActive = false;
}

RPT::RPT() {
  reset();
}

bool RPT::contains(uint64_t pc) const {
  return m_RPT_entries.count(pc);
}

void RPT::activate(uint64_t pc, bool br_taken) {
  if (!contains(pc)) {
    m_RPT_entries[pc] = RPT_entry();
    m_RPT_entries[pc].initialize(pc);
  }
  RPT_entry &entry = m_RPT_entries[pc];
  if (!m_active_record[m_current_call_depth].count(pc)) {
    bool entry_st_bits = entry.increase_call_level();
    m_active_record[m_current_call_depth][pc] = entry_st_bits;
  }
  entry.activate_entry(br_taken);
}

void RPT::deactivate_all() {
  for (auto &i : m_active_record[m_current_call_depth]) {
    const auto pc = i.first;
    const auto st_bits = i.second;
    m_RPT_entries[pc].early_deactivate_entry();
  }
}

void RPT::train(uint64_t commit_pc) {
  // TODO: Don't train the entry if a highly bias branch taken to the opposite direction
  // Discover the nested branch?
  for (auto &i : m_active_record[m_current_call_depth]) {
    const auto entry_pc = i.first;
    m_RPT_entries[entry_pc].train_entry(commit_pc);
  }
}

void RPT::increase_call_level() {
  /*
   * at times when the main thread executes at a greater call
   * level than the training branch, the RPT entry is temporarily
   * inactivated (but another instance of the same branch may in
   * fact be active and training the RPT entry)
   */
  assert (m_current_call_depth < (LIMIT_MAX_CALL_DEPTH - 1));
  m_current_call_depth++;
  m_active_record[m_current_call_depth].clear();
}

void RPT::decrease_call_level() {
  /*
   * When control returns to the same call level as the training branch, the entry is
   * again activated. When the function containing an active RPT
   * entry returns, that entry is inactivated.
   */
  assert(0 < m_current_call_depth);
  // Restore the old level's state bits for entries that have been activated in this level
  for (auto &i : m_active_record[m_current_call_depth]) {
    const auto pc = i.first;
    const auto st_bits = i.second;
    m_RPT_entries[pc].decrease_call_level(st_bits);
  }
  m_current_call_depth--;
}

void RPT::reset() {
  m_current_call_depth = INIT_CALL_DEPTH;
  for (auto &i : m_active_record) {
    i.clear();
  }
}

std::string RPT::dump_result() {
  static const char *const cat_name[] = {
    "Return",
    "Below",
    "Above",
    "Rebound"
  };
  static const char *const reason_name[] = {
    "[0] Unknown",
    "[1] All hit return",
    "[2] Reach first",
    "[3] Always reach whether taken or not taken",
    "[4] Always reach only taken or not taken",
    "[5] Fallback to BelowPotential"
  };
  std::stringstream output_stream;
  output_stream << std::left << std::setw(16) << std::setfill(' ') << "BranchPC";
  output_stream << ", ";
  output_stream << std::left << std::setw(16) << std::setfill(' ') << "ReconvPoint";
  output_stream << ", ";
  output_stream << std::left << std::setw(8) << std::setfill(' ') << "ActCnt";
  output_stream << ", ";
  output_stream << std::left << std::setw(8) << std::setfill(' ') << "DeactCnt";
  output_stream << ", ";
  output_stream << std::left << std::setw(7) << std::setfill(' ') << "RecCat";
  output_stream << ", ";
  output_stream << std::left << std::setw(0) << std::setfill(' ') << "Reason";
  output_stream << std::endl;

  for (auto &i: m_RPT_entries) {
    const auto entry_pc = i.first;
    const auto &entry = i.second;
    RPT_entry::prediction_details pred_reason = {};
    const auto reconv_pc = entry.make_prediction(&pred_reason);

    // BranchPC
    output_stream << std::right << "" << std::setw(16) << std::setfill('0') << std::setbase(16)
                  << entry_pc;
    output_stream << ", ";
    // ReconvPoint
    output_stream << std::right << "" << std::setw(16) << std::setfill('0') << std::setbase(16)
                  << reconv_pc;
    output_stream << ", ";
    // ActivateCnt
    output_stream << std::right << std::setw(8) << std::setfill(' ') << std::setbase(10)
                  << entry.get_activate_cnt();
    output_stream << ", ";
    // DeactivateCnt
    output_stream << std::right << std::setw(8) << std::setfill(' ') << std::setbase(10)
                  << entry.get_early_deactivate_cnt();
    output_stream << ", ";
    // RecCat
    output_stream << std::left << std::setw(7) << std::setfill(' ')
                  << cat_name[pred_reason.cat_id];
    output_stream << ", ";
    // Reason
    output_stream << std::right << reason_name[pred_reason.reason_id];
    output_stream << std::endl;
  }

  return output_stream.str();
}

const BFT::br_stat &BFT::get_stat(uint64_t pc) {
  static const BFT::br_stat null_result = {};
  if (m_branches_history.count(pc))
    return m_branches_history[pc];
  else
    return null_result;
}

bool BFT::is_filtered(uint64_t pc) {
  static const uint64_t SAMPLE_THRESHOLD = 30;//1; // only stop filtering if we have sampled at least SAMPLE_THRESHOLD,
  static const double BIAS_THRESHOLD = 0.95;//1;   // and the the bias rate is lower than this

  if (!m_branches_history.count(pc))
    return true; // filter out the pc that never be trained

  const br_stat &br_history = m_branches_history[pc];

  uint64_t cnt_total = br_history.total_cnt;
  uint64_t cnt_major = br_history.curr_major_cnt;

  return !(
    (SAMPLE_THRESHOLD < cnt_total) &&
    ((double) cnt_major / cnt_total) < BIAS_THRESHOLD
  );
}

void BFT::train(uint64_t pc, uint64_t npc, bool branch_taken) {
  // Don't train if the BFT stats are statically m_loaded from a CSV file
  if (m_static_stats)
    return;

  if (!m_branches_history.count(pc)) {
    m_branches_history[pc] = {};
  }
  br_stat &br_history = m_branches_history[pc];

  ++br_history.total_cnt;
  uint64_t curr_target_cnt = ++br_history.cnt_by_br_target[npc];
  if (br_history.curr_major_cnt < curr_target_cnt) {
    br_history.curr_major_cnt = curr_target_cnt;
    br_history.curr_major_target = npc;
  }
}

std::string BFT::dump_result() {
  std::stringstream output_stream;
  output_stream << std::left << std::setw(16) << std::setfill(' ') << "BranchPC";
  output_stream << ", ";
  output_stream << std::left << std::setw(8) << std::setfill(' ') << "TotalCnt";
  output_stream << ", ";
  output_stream << std::left << std::setw(5) << std::setfill(' ') << "BiasRate";
  output_stream << ", ";
  output_stream << std::left << std::setw(16) << std::setfill(' ') << "MajorTarget";
  output_stream << ", ";
  output_stream << std::left << std::setw(8) << std::setfill(' ') << "MajorCnt";
  output_stream << ", ";
  output_stream << std::left << std::setw(0) << std::setfill(' ') << "Details...";
  output_stream << std::endl;

  for (auto &i: m_branches_history) {
    const auto entry_pc = i.first;
    const auto &entry_stat = i.second;
    // BranchPC
    output_stream << std::right << "" << std::setw(16) << std::setfill('0') << std::setbase(16)
                  << entry_pc;
    output_stream << ", ";
    // TotalCnt
    output_stream << std::left << std::setw(8) << std::setfill(' ') << std::setbase(10)
                  << entry_stat.total_cnt;
    output_stream << ", ";
    // BiasRate
    output_stream << std::right << std::setw(8) << std::setfill(' ') << std::fixed
                  << std::setprecision(3)
                  << (float) entry_stat.curr_major_cnt / entry_stat.total_cnt;
    output_stream << ", ";
    // MajorTarget
    output_stream << std::right << "" << std::setw(16) << std::setfill('0') << std::setbase(16)
                  << entry_stat.curr_major_target;
    output_stream << ", ";
    // MajorCnt
    output_stream << std::right << std::setw(8) << std::setfill(' ') << std::setbase(10)
                  << entry_stat.curr_major_cnt;

    for (auto &j: entry_stat.cnt_by_br_target) {
      output_stream << ", ";
      output_stream << std::right << std::setw(16) << std::setfill('0') << std::setbase(16)
                    << j.first << ":";
      output_stream << std::left << std::setw(8) << std::setfill(' ') << std::setbase(10)
                    << j.second;
    }

    output_stream << std::endl;
  }

  return output_stream.str();
}

bool BFT::load_static_stat_from_file(const std::string &filepath) {
  std::ifstream stat_file;
  stat_file.open(filepath, std::ios::in);
  if (!stat_file.good()) {
    this->m_static_stats = false;
    return false;
  }

  std::string line;
  std::getline(stat_file, line); // skip over the header line
  std::vector<std::string> parsed_line;
  parseCSVLine(line, &parsed_line);
  assert(parsed_line.size() == 6);
  assert(parsed_line[0] == "BranchPC");
  assert(parsed_line[1] == "TotalCnt");
  assert(parsed_line[2] == "BiasRate");
  assert(parsed_line[3] == "MajorTarget");
  assert(parsed_line[4] == "MajorCnt");
  assert(parsed_line[5] == "Details...");
  m_branches_history.clear();
  while (stat_file.good()) {
    std::getline(stat_file, line);
    if (trim(line).empty())
      break;
    parseCSVLine(line, &parsed_line);
    assert(parsed_line.size() >= 6);
    uint64_t branch_pc = std::stoull(parsed_line[0], nullptr, 16);
    uint64_t total_cnt = std::stoull(parsed_line[1]);
    uint64_t major_target = std::stoull(parsed_line[3], nullptr, 16);
    uint64_t major_cnt = std::stoull(parsed_line[4]);
    uint64_t calc_total = 0;
    assert(!m_branches_history.count(branch_pc));
    m_branches_history[branch_pc] = {};
    auto &entry_stat = m_branches_history[branch_pc];
    entry_stat.total_cnt = total_cnt;
    entry_stat.curr_major_target = major_target;
    entry_stat.curr_major_cnt = major_cnt;
    for (size_t i = 5; i < parsed_line.size(); i++) {
      auto lr_pair = rsplit(parsed_line[i], ':');
      uint64_t target_pc = std::stoull(lr_pair.first, nullptr, 16);
      uint64_t target_cnt = std::stoull(lr_pair.second);
      calc_total += target_cnt;
      assert(target_cnt <= major_cnt);
      assert(!entry_stat.cnt_by_br_target.count(target_pc));
      entry_stat.cnt_by_br_target[target_pc] = target_cnt;
    }
    assert(calc_total == total_cnt);
  }
  this->m_static_stats = true;
  return true;
}

bool BFT::is_uncommon_target(uint64_t pc, uint64_t npc) {
  if (!m_static_stats) {
    // for now, let's only enable this if the BFT is working in static mode (stats are m_loaded from a CSV)
    return false;
  }

  // if the sample of branch is less than this number, all it's target will be deem as uncommon, as the branch itself
  // is uncommon
  static const uint64_t SAMPLE_THRESHOLD = 30;
  // if branch must has a bias rate lower than this, any path will be deem as common path
  static const double UNCOMMON_THRESHOLD = 0.95;

  if (!m_branches_history.count(pc)) {
    if (m_static_stats) {
      std::stringstream ss;
      ss << std::hex << pc;
      throw std::runtime_error("The m_loaded BFT doesn't contains stats for branch PC=" + ss.str());
    }
    // If the BFT is being trained on the fly, conservatively deem any target of a not seen branch to be uncommon
    // in this way we are trading some potential training opportunity for a better (the most frequent) reconv point
    // however this shouldn't be reached for now, as we don't allow such configuration
    assert(0);
    return true;
  }

  const br_stat &br_history = m_branches_history[pc];

  uint64_t cnt_total = br_history.total_cnt;
  uint64_t cnt_major = br_history.curr_major_cnt;

  if (cnt_total < SAMPLE_THRESHOLD) {
    // If the BranchPC is less frequently seen, the current path itself is a less frequent path
    // so deem any target on it as less frequent target
    return true;
  }

  double biased_rate = ((double) cnt_major / cnt_total);
  if (UNCOMMON_THRESHOLD <= biased_rate) {
    // As this is a highly biased branch, we can know the target's commonality by comparing npc with it's major target
    return npc != br_history.curr_major_target;
  } else {
    // If this branch is not highly biased, we just simple think any path is common for now
    return false;
  }
}


void reconv_predictor::on_branch_retired(uint64_t pc, uint64_t npc, bool outcome) {
  m_BFT.train(pc, npc, outcome);
  if (m_BFT.is_uncommon_target(pc, npc)) {
    // if we are going to get on an uncommon path, don't let it mess up our most frequently reconv point estimation
    m_RPT.deactivate_all();
  } else {
    m_RPT.train(pc);
    if (m_RPT.contains(pc) || !m_BFT.is_filtered(pc))
      // activate the entry, with branch outcome
      m_RPT.activate(pc, outcome);
  }
}

void reconv_predictor::on_indirect_jmp_retired(uint64_t pc, uint64_t npc) {
  // activate the entry with a always taken branch outcome (always taken)
  on_branch_retired(pc, npc, true);
}

void reconv_predictor::on_other_insn_retired(uint64_t pc) {
  // train the entry
  m_RPT.train(pc);
}

void reconv_predictor::on_function_call(uint64_t pc, uint64_t target_addr) {
  // increase the call level
  m_RPT.increase_call_level();
}

void reconv_predictor::on_function_return(uint64_t pc, uint64_t return_addr) {
  // decrease the call level
  m_RPT.decrease_call_level();
}

std::string reconv_predictor::dump_RPT_result_csv() {
  return m_RPT.dump_result();
}

std::string reconv_predictor::dump_BFT_result_csv() {
  return m_BFT.dump_result();
}

bool static_reconv_predictor::load_from_csv(const std::string &filepath) {
  std::ifstream stat_file;
  stat_file.open(filepath, std::ios::in);
  if (!stat_file.good()) {
    return false;
  }

  std::string line;
  std::getline(stat_file, line); // skip over the header line
  std::vector<std::string> parsed_line;
  parseCSVLine(line, &parsed_line);
  assert(parsed_line.size() == 6);
  assert(parsed_line[0] == "BranchPC");
  assert(parsed_line[1] == "ReconvPoint");
  assert(parsed_line[2] == "ActCnt");
  assert(parsed_line[3] == "DeactCnt");
  assert(parsed_line[4] == "RecCat");
  assert(parsed_line[5] == "Reason");
  m_br_reconv_point.clear();
  while (stat_file.good()) {
    std::getline(stat_file, line);
    if (trim(line).empty())
      break;
    parseCSVLine(line, &parsed_line);
    assert(parsed_line.size() == 6);
    uint64_t branch_pc = std::stoull(parsed_line[0], nullptr, 16);
    uint64_t reconv_point = std::stoull(parsed_line[1], nullptr, 16);
    m_br_reconv_point[branch_pc] = reconv_point;
  }
  m_loaded = true;
  return true;
}
