#ifndef SPPAM_B_CONFIG_H
#define SPPAM_B_CONFIG_H

#include <string>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <map>
#include <stdexcept>

struct sppam_b_config {
  // General SPPAM parameters
  std::string bp_type = "hashed_perceptron";
  std::string ip_style = "delta";
  std::string global_type = "ordered_recent_segment_history";
  unsigned int global_bits = 256;
  unsigned int local_bits = 64;
  unsigned int segment_bits = 64;
  int alignment_factor = 0;

  // Per-predictor parameters (all stored; only the active BP's params are used)
  // Bimodal
  unsigned int bimodal_table_size = 16384;

  // Gshare
  unsigned int gshare_table_size = 16384;
  unsigned int gshare_history_length = 14;

  // Hashed Perceptron
  unsigned int hp_num_tables = 16;
  unsigned int hp_table_size = 4096;
  unsigned int hp_min_history = 3;
  unsigned int hp_max_history = 232;

  // MPP
  unsigned int mpp_num_tables = 16;
  unsigned int mpp_table_size = 4096;

  // TAGE-SC-L
  unsigned int tage_logb = 13;
  unsigned int tage_logg = 10;
  unsigned int tage_minhist = 6;
  unsigned int tage_maxhist = 256;
  unsigned int tage_logl = 5;

  // Throttling controls
  bool use_ip_conf = true;       // use per-IP confidence table to gate prefetches
  bool do_lookahead = true;      // enable lookahead prefetching beyond the first prediction
  bool prob_drop = false;        // probabilistically drop prefetches based on confidence
  bool ip_prefetch_degree = false; // use per-IP prefetch degree instead of global PREFETCH_DEGREE
  bool ip_lookahead = false;     // use per-IP lookahead depth instead of global MAX_LOOKAHEAD

  // Numeric throttle parameters
  int64_t prefetch_degree = 8;       // global max prefetch degree
  double min_lookahead_conf = 0.2;   // minimum confidence required to continue lookahead
  double min_l1d_conf = 0.9;         // minimum confidence required to prefetch into L1D
  int64_t max_lookahead = 64;        // maximum lookahead depth
  double timeliness_cycle = 10000;   // cycle window for timeliness determination

  static std::string trim(const std::string& s) {
    auto start = s.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) return "";
    auto end = s.find_last_not_of(" \t\r\n");
    return s.substr(start, end - start + 1);
  }

  static std::string to_lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    return s;
  }

  static sppam_b_config load_from_file(const std::string& path) {
    sppam_b_config cfg;
    std::ifstream file(path);
    if (!file.is_open()) return cfg; // return defaults if file not found

    std::string line;
    while (std::getline(file, line)) {
      // Strip comments
      auto comment_pos = line.find('#');
      if (comment_pos != std::string::npos)
        line = line.substr(0, comment_pos);

      auto eq_pos = line.find('=');
      if (eq_pos == std::string::npos) continue;

      std::string key = to_lower(trim(line.substr(0, eq_pos)));
      std::string val = to_lower(trim(line.substr(eq_pos + 1)));

      if (key.empty() || val.empty()) continue;

      if (key == "bp_type") cfg.bp_type = val;
      else if (key == "ip_style") cfg.ip_style = val;
      else if (key == "global_type") cfg.global_type = val;
      else if (key == "global_bits") cfg.global_bits = static_cast<unsigned int>(std::stoul(val));
      else if (key == "local_bits") cfg.local_bits = static_cast<unsigned int>(std::stoul(val));
      else if (key == "segment_bits") cfg.segment_bits = static_cast<unsigned int>(std::stoul(val));
      else if (key == "alignment_factor") cfg.alignment_factor = std::stoi(val);
      // Bimodal
      else if (key == "bimodal_table_size") cfg.bimodal_table_size = static_cast<unsigned int>(std::stoul(val));
      // Gshare
      else if (key == "gshare_table_size") cfg.gshare_table_size = static_cast<unsigned int>(std::stoul(val));
      else if (key == "gshare_history_length") cfg.gshare_history_length = static_cast<unsigned int>(std::stoul(val));
      // Hashed Perceptron
      else if (key == "hp_num_tables") cfg.hp_num_tables = static_cast<unsigned int>(std::stoul(val));
      else if (key == "hp_table_size") cfg.hp_table_size = static_cast<unsigned int>(std::stoul(val));
      else if (key == "hp_min_history") cfg.hp_min_history = static_cast<unsigned int>(std::stoul(val));
      else if (key == "hp_max_history") cfg.hp_max_history = static_cast<unsigned int>(std::stoul(val));
      // MPP
      else if (key == "mpp_num_tables") cfg.mpp_num_tables = static_cast<unsigned int>(std::stoul(val));
      else if (key == "mpp_table_size") cfg.mpp_table_size = static_cast<unsigned int>(std::stoul(val));
      // TAGE-SC-L
      else if (key == "tage_logb") cfg.tage_logb = static_cast<unsigned int>(std::stoul(val));
      else if (key == "tage_logg") cfg.tage_logg = static_cast<unsigned int>(std::stoul(val));
      else if (key == "tage_minhist") cfg.tage_minhist = static_cast<unsigned int>(std::stoul(val));
      else if (key == "tage_maxhist") cfg.tage_maxhist = static_cast<unsigned int>(std::stoul(val));
      else if (key == "tage_logl") cfg.tage_logl = static_cast<unsigned int>(std::stoul(val));
      // Throttling
      else if (key == "use_ip_conf") cfg.use_ip_conf = (val == "true" || val == "1");
      else if (key == "do_lookahead") cfg.do_lookahead = (val == "true" || val == "1");
      else if (key == "prob_drop") cfg.prob_drop = (val == "true" || val == "1");
      else if (key == "ip_prefetch_degree") cfg.ip_prefetch_degree = (val == "true" || val == "1");
      else if (key == "ip_lookahead") cfg.ip_lookahead = (val == "true" || val == "1");
      // Numeric throttle params
      else if (key == "prefetch_degree") cfg.prefetch_degree = static_cast<int64_t>(std::stoll(val));
      else if (key == "min_lookahead_conf") cfg.min_lookahead_conf = std::stod(val);
      else if (key == "min_l1d_conf") cfg.min_l1d_conf = std::stod(val);
      else if (key == "max_lookahead") cfg.max_lookahead = static_cast<int64_t>(std::stoll(val));
      else if (key == "timeliness_cycle") cfg.timeliness_cycle = std::stod(val);
    }

    return cfg;
  }

  static sppam_b_config load() {
    std::string path;
    if (const char* env = std::getenv("SPPAM_B_CONFIG"); env != nullptr)
      path = env;
    else
      path = "sppam_b.cfg";
    return load_from_file(path);
  }
};

#endif
