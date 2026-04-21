#ifndef BP_TAGE_SC_L_H
#define BP_TAGE_SC_L_H

#include <array>
#include <cstdint>
#include <cstring>
#include <cmath>
#include <cassert>

#include "../branch_predictor.h"
#include "msl/fwcounter.h"

namespace sppam_bp {

struct tage_sc_l : branch_predictor {
  // ======================== TAGE PARAMETERS ========================
  static constexpr int NHIST = 36;           // number of tagged tables (pairs)
  static constexpr int NBANKLOW = 10;
  static constexpr int NBANKHIGH = 20;
  static constexpr int BORN = 13;
  static constexpr int BORNINFASSOC = 9;
  static constexpr int BORNSUPASSOC = 23;
  static constexpr int MINHIST = 6;
  static constexpr int MAXHIST = 256;        // capped to BP_GLOBAL_BITS
  static constexpr int LOGG = 16;
  static constexpr int TBITS = 8;
  static constexpr int PHISTWIDTH = 27;
  static constexpr int UWIDTH = 1;
  static constexpr int CWIDTH = 3;
  static constexpr int NNN = 1;
  static constexpr int HYSTSHIFT = 2;
  static constexpr int LOGB = 16;
  static constexpr int BORNTICK = 1024;

  // Alternate prediction selection
  static constexpr int LOGSIZEUSEALT = 4;
  static constexpr int ALTWIDTH = 5;
  static constexpr int SIZEUSEALT = (1 << LOGSIZEUSEALT);

  // Statistical corrector parameters
  static constexpr int PERCWIDTH = 6;
  static constexpr int LOGBIAS = 8;
  static constexpr int LOGGNB = 10;
  static constexpr int GNB = 3;
  static constexpr int LOGPNB = 9;
  static constexpr int PNB = 3;
  static constexpr int LOGLNB = 10;
  static constexpr int LNB = 3;
  static constexpr int LOGSNB = 9;
  static constexpr int SNB = 3;
  static constexpr int LOGTNB = 10;
  static constexpr int TNB = 2;

  static constexpr int WIDTHRES = 12;
  static constexpr int WIDTHRESP = 8;
  static constexpr int LOGSIZEUP = 6;
  static constexpr int LOGSIZEUPS = (LOGSIZEUP / 2);
  static constexpr int EWIDTH = 6;
  static constexpr int CONFWIDTH = 7;

  // Loop predictor parameters
  static constexpr int LOGL = 5;
  static constexpr int WIDTHNBITERLOOP = 10;
  static constexpr int LOOPTAG = 10;
  static constexpr int CONFLOOP = 15;

  // Runtime-configurable parameters (defaults = compile-time constants)
  int rt_logb = LOGB;
  int rt_logg = LOGG;
  int rt_minhist = MINHIST;
  int rt_maxhist = MAXHIST;
  int rt_logl = LOGL;

  void configure(unsigned int logb, unsigned int logg, unsigned int minhist,
                 unsigned int maxhist, unsigned int logl) {
    rt_logb = std::min(static_cast<int>(logb), LOGB);
    if (rt_logb < 1) rt_logb = 1;
    rt_logg = std::min(static_cast<int>(logg), LOGG);
    if (rt_logg < 1) rt_logg = 1;
    rt_minhist = std::max(1, static_cast<int>(minhist));
    rt_maxhist = std::min(static_cast<int>(maxhist), MAXHIST);
    if (rt_maxhist < rt_minhist) rt_maxhist = rt_minhist;
    rt_logl = std::min(static_cast<int>(logl), LOGL);
    if (rt_logl < 2) rt_logl = 2; // need at least 2 for (LOGL-2) shifts
  }

  static constexpr bool DEBUG = false;

  // ======================== TAGE TABLES ========================
  struct bentry {
    int8_t hyst = 1;
    int8_t pred = 0;
  };

  struct gentry {
    int8_t ctr = 0;
    uint32_t tag = 0;
    int8_t u = 0;
  };

  struct lentry {
    uint16_t NbIter = 0;
    uint8_t confid = 0;
    uint16_t CurrentIter = 0;
    uint16_t TAG = 0;
    uint8_t age = 0;
    bool dir = false;
  };

  // Bimodal table
  std::array<bentry, (1 << LOGB)> btable{};

  // Tagged TAGE tables: low + high banks
  std::array<gentry, NBANKLOW * (1 << LOGG)> gtable_low{};
  std::array<gentry, NBANKHIGH * (1 << LOGG)> gtable_high{};

  // Table pointers (indices into gtable_low or gtable_high)
  // We use offsets: for tables 1..BORN-1 -> gtable_low, BORN..NHIST -> gtable_high
  gentry* get_gtable(int bank) {
    if(bank >= BORN) return gtable_high.data();
    return gtable_low.data();
  }

  // History lengths and table config
  std::array<int, NHIST + 1> m{};
  std::array<int, NHIST + 1> TB{};
  std::array<int, NHIST + 1> logg_arr{};
  std::array<int, NHIST + 1> SizeTable{};
  std::array<bool, NHIST + 1> NOSKIP{};

  // Prediction state
  std::array<int, NHIST + 1> GI{};
  std::array<uint32_t, NHIST + 1> GTAG{};
  int BI = 0;
  bool pred_taken = false;
  bool alttaken = false;
  bool tage_pred = false;
  bool LongestMatchPred = false;
  int HitBank = 0;
  int AltBank = 0;
  int Seed = 0;
  bool pred_inter = false;

  bool LowConf = false;
  bool HighConf = false;
  bool MedConf = false;
  bool AltConf = false;
  int8_t BIM = 0;

  int TICK = 0;

  int8_t use_alt_on_na[SIZEUSEALT] = {};

  // SC tables
  int8_t Bias[1 << LOGBIAS] = {};
  int8_t BiasSK[1 << LOGBIAS] = {};
  int8_t BiasBank[1 << LOGBIAS] = {};

  // Use a max log size for all GEHL tables so pointer arrays work uniformly
  static constexpr int LOGMAXGEHL = LOGGNB; // max(LOGGNB, LOGPNB, LOGLNB, LOGSNB, LOGTNB) = 10

  int Gm[GNB] = {40, 24, 10};
  int8_t GGEHL_store[GNB][1 << LOGGNB] = {};
  int8_t* GGEHL[GNB] = {};

  int Pm[PNB] = {25, 16, 9};
  int8_t PGEHL_store[PNB][1 << LOGPNB] = {};
  int8_t* PGEHL[PNB] = {};

  int Lm[LNB] = {11, 6, 3};
  int8_t LGEHL_store[LNB][1 << LOGLNB] = {};
  int8_t* LGEHL[LNB] = {};

  int Sm[SNB] = {16, 11, 6};
  int8_t SGEHL_store[SNB][1 << LOGSNB] = {};
  int8_t* SGEHL[SNB] = {};

  int Tm[TNB] = {9, 4};
  int8_t TGEHL_store[TNB][1 << LOGTNB] = {};
  int8_t* TGEHL[TNB] = {};

  int8_t WG[1 << LOGSIZEUPS] = {};
  int8_t WL[1 << LOGSIZEUPS] = {};
  int8_t WS[1 << LOGSIZEUPS] = {};
  int8_t WT[1 << LOGSIZEUPS] = {};
  int8_t WP[1 << LOGSIZEUPS] = {};
  int8_t WB[1 << LOGSIZEUPS] = {};

  int updatethreshold = 0;
  int Pupdatethreshold[1 << LOGSIZEUP] = {};

  int LSUM = 0;
  int THRES = 0;
  int8_t FirstH = 0;
  int8_t SecondH = 0;

  // SC local/global history constants (index computation helpers)
  // The actual history state lives in bp_context, provided by SPPAM.
  static constexpr int LOGLOCAL = 8;
  static constexpr int NLOCAL = (1 << LOGLOCAL);
  static constexpr int LOGSECLOCAL = 4;
  static constexpr int NSECLOCAL = (1 << LOGSECLOCAL);
  static constexpr int NTLOCAL = 16;

  // Loop predictor
  std::array<lentry, (1 << LOGL)> ltable{};
  bool predloop = false;
  int LIB = 0, LI = 0, LHIT = -1;
  int LTAG = 0;
  bool LVALID = false;
  int8_t WITHLOOP = -1;

  // Path history (derived from IP stream) — now provided by bp_context

  // Stats
  uint64_t predict_taken_count = 0;
  uint64_t predict_nottaken_count = 0;
  uint64_t outcome_taken_count = 0;
  uint64_t outcome_nottaken_count = 0;

  using branch_predictor::branch_predictor;

  // ======================== HELPERS ========================

  int bindex(uint64_t PC) const { return ((PC ^ (PC >> rt_logb)) & ((1 << rt_logb) - 1)); }

  int F(long long A, int size, int bank) const;

  // Fold global_hist bitset for a given history length into compressed_length bits
  unsigned int fold_history(const dynamic_bitset& hist, int history_length, int compressed_length) const;

  // Compute TAGE index for a bank using provided global_hist
  int gindex(unsigned int PC, int bank, long long hist_path,
             const dynamic_bitset& global_hist) const;

  // Compute TAGE tag for a bank
  uint16_t gtag(unsigned int PC, int bank,
                const dynamic_bitset& global_hist) const;

  static void ctrupdate(int8_t& ctr, bool taken, int nbits);

  bool getbim();
  void baseupdate(bool Taken);

  int MYRANDOM();

  // TAGE prediction using provided global_hist and path history from context
  void Tagepred(uint64_t PC, const dynamic_bitset& global_hist, long long phist);

  // Loop predictor
  bool getloop(uint64_t PC);
  void loopupdate(uint64_t PC, bool Taken, bool ALLOC);
  int lindex(uint64_t PC) const { return (((PC ^ (PC >> 2)) & ((1 << (rt_logl - 2)) - 1)) << 2); }

  // SC prediction
  int Gpredict(uint64_t PC, long long BHIST, int* length, int8_t** tab, int NBR, int logs, int8_t* W, int logsize) const;
  void Gupdate(uint64_t PC, bool taken, long long BHIST, int* length, int8_t** tab, int NBR, int logs, int8_t* W, int logsize);

  // Get SC index helpers
  unsigned int INDUPD_fn(uint64_t PC) const { return (PC ^ (PC >> 2)) & ((1 << LOGSIZEUP) - 1); }
  unsigned int INDUPDS_fn(uint64_t PC) const { return (PC ^ (PC >> 2)) & ((1 << LOGSIZEUPS) - 1); }
  unsigned int INDLOCAL_fn(uint64_t PC) const { return (PC ^ (PC >> 2)) & (NLOCAL - 1); }
  unsigned int INDSLOCAL_fn(uint64_t PC) const { return (PC ^ (PC >> 5)) & (NSECLOCAL - 1); }
  unsigned int INDTLOCAL_fn(uint64_t PC) const { return (PC ^ (PC >> LOGTNB)) & (NTLOCAL - 1); }

  // Full prediction
  bool GetPrediction(uint64_t PC, const dynamic_bitset& global_hist,
                     const dynamic_bitset& local_hist,
                     const bp_context& ctx);

  // Full update
  void UpdatePredictor(uint64_t PC, bool resolveDir, bool predDir,
                       const dynamic_bitset& global_hist,
                       const dynamic_bitset& local_hist,
                       const bp_context& ctx);

  // ======================== INTERFACE ========================
  virtual void initialize_branch_predictor();

  virtual void last_branch_result(champsim::address ip, const dynamic_bitset& global_hist, const dynamic_bitset& local_hist, bool taken, bp_context& ctx);
  virtual std::pair<bool,double> predict_branch(champsim::address ip, const dynamic_bitset& global_hist, const dynamic_bitset& local_hist, const bp_context& ctx);

  void print_heartbeat();
  void print_stats();

private:
  // Store last prediction for update
  bool last_pred = false;
};

}

#endif
