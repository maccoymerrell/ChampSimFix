#include "tage_sc_l.h"

namespace sppam_bp {

// ======================== INITIALIZATION ========================

void tage_sc_l::initialize_branch_predictor() {
  // Compute geometric history lengths
  m[1] = MINHIST;
  m[NHIST / 2] = MAXHIST;
  for(int i = 2; i <= NHIST / 2; i++) {
    m[i] = static_cast<int>(
      MINHIST * std::pow(static_cast<double>(MAXHIST) / MINHIST,
                         static_cast<double>(i - 1) / (NHIST / 2 - 1)) + 0.5);
  }
  for(int i = 1; i <= NHIST; i++) {
    NOSKIP[i] = ((i - 1) & 1) || ((i >= BORNINFASSOC) & (i < BORNSUPASSOC));
  }
  NOSKIP[4] = false;
  NOSKIP[NHIST - 2] = false;
  NOSKIP[8] = false;
  NOSKIP[NHIST - 6] = false;

  for(int i = NHIST; i > 1; i--) {
    m[i] = m[(i + 1) / 2];
  }
  for(int i = 1; i <= NHIST; i++) {
    // Cap history length to provided global_hist size (which may differ from old BP_GLOBAL_BITS)
    if(m[i] > 256) m[i] = 256; // keep the cap at hard maximum; actual bitset may vary
    TB[i] = TBITS + 4 * (i >= BORN);
    logg_arr[i] = LOGG;
  }

  SizeTable[1] = NBANKLOW * (1 << LOGG);
  SizeTable[BORN] = NBANKHIGH * (1 << LOGG);

  // Init bimodal
  for(auto& b : btable) { b.pred = 0; b.hyst = 1; }

  // Init use_alt_on_na
  std::memset(use_alt_on_na, 0, sizeof(use_alt_on_na));

  // Init SC tables
  updatethreshold = 35 << 3;
  std::memset(Pupdatethreshold, 0, sizeof(Pupdatethreshold));

  // Set up pointer arrays into storage
  for(int i = 0; i < GNB; i++) GGEHL[i] = GGEHL_store[i];
  for(int i = 0; i < PNB; i++) PGEHL[i] = PGEHL_store[i];
  for(int i = 0; i < LNB; i++) LGEHL[i] = LGEHL_store[i];
  for(int i = 0; i < SNB; i++) SGEHL[i] = SGEHL_store[i];
  for(int i = 0; i < TNB; i++) TGEHL[i] = TGEHL_store[i];

  for(int i = 0; i < GNB; i++)
    for(int j = 0; j < (1 << LOGGNB); j++)
      GGEHL_store[i][j] = (!(j & 1)) ? -1 : 0;

  for(int i = 0; i < LNB; i++)
    for(int j = 0; j < (1 << LOGLNB); j++)
      LGEHL_store[i][j] = (!(j & 1)) ? -1 : 0;

  for(int i = 0; i < SNB; i++)
    for(int j = 0; j < (1 << LOGSNB); j++)
      SGEHL_store[i][j] = (!(j & 1)) ? -1 : 0;

  for(int i = 0; i < TNB; i++)
    for(int j = 0; j < (1 << LOGTNB); j++)
      TGEHL_store[i][j] = (!(j & 1)) ? -1 : 0;

  for(int i = 0; i < PNB; i++)
    for(int j = 0; j < (1 << LOGPNB); j++)
      PGEHL_store[i][j] = (!(j & 1)) ? -1 : 0;

  // Init SC bias tables
  for(int j = 0; j < (1 << LOGBIAS); j++) {
    switch(j & 3) {
      case 0: Bias[j] = -32; break;
      case 1: Bias[j] = 31; break;
      case 2: Bias[j] = -1; break;
      case 3: Bias[j] = 0; break;
    }
  }
  for(int j = 0; j < (1 << LOGBIAS); j++) {
    switch(j & 3) {
      case 0: BiasSK[j] = -8; break;
      case 1: BiasSK[j] = 7; break;
      case 2: BiasSK[j] = -32; break;
      case 3: BiasSK[j] = 31; break;
    }
  }
  for(int j = 0; j < (1 << LOGBIAS); j++) {
    switch(j & 3) {
      case 0: BiasBank[j] = -32; break;
      case 1: BiasBank[j] = 31; break;
      case 2: BiasBank[j] = -1; break;
      case 3: BiasBank[j] = 0; break;
    }
  }

  for(int i = 0; i < (1 << LOGSIZEUPS); i++) {
    WG[i] = 7; WL[i] = 7; WS[i] = 7;
    WT[i] = 7; WP[i] = 7; WB[i] = 4;
  }

  // Internal streaming histories are now provided via bp_context.
  // No local history init needed here.

  TICK = 0;
  Seed = 0;

  LVALID = false;
  WITHLOOP = -1;
}

// ======================== FOLDING UTILITIES ========================

unsigned int tage_sc_l::fold_history(const dynamic_bitset& hist,
                                      int history_length, int compressed_length) const
{
  if(compressed_length <= 0 || history_length <= 0) return 0;
  unsigned int mask = (1u << compressed_length) - 1;
  unsigned int comp = 0;
  int len = std::min(history_length, static_cast<int>(hist.size()));

  // Chunk-XOR fold: split history into compressed_length-bit chunks
  // and XOR them together, using word-level extraction for speed.
  for(int pos = 0; pos < len; pos += compressed_length) {
    int chunk_bits = std::min(compressed_length, len - pos);
    uint64_t bits = hist.extract(static_cast<std::size_t>(pos),
                                  static_cast<std::size_t>(chunk_bits));
    comp ^= static_cast<unsigned int>(bits);
  }
  return comp & mask;
}

int tage_sc_l::F(long long A, int size, int bank) const {
  int A1, A2;
  A = A & ((1LL << size) - 1);
  A1 = (A & ((1 << logg_arr[bank]) - 1));
  A2 = (A >> logg_arr[bank]);
  if(bank < logg_arr[bank])
    A2 = ((A2 << bank) & ((1 << logg_arr[bank]) - 1)) + (A2 >> (logg_arr[bank] - bank));
  A = A1 ^ A2;
  if(bank < logg_arr[bank])
    A = ((A << bank) & ((1 << logg_arr[bank]) - 1)) + (A >> (logg_arr[bank] - bank));
  return static_cast<int>(A);
}

int tage_sc_l::gindex(unsigned int PC, int bank, long long hist_path,
                       const dynamic_bitset& global_hist) const
{
  int M = (m[bank] > PHISTWIDTH) ? PHISTWIDTH : m[bank];
  unsigned int ch_i_comp = fold_history(global_hist, m[bank], logg_arr[bank]);
  int index = PC ^ (PC >> (std::abs(logg_arr[bank] - bank) + 1)) ^ ch_i_comp ^ F(hist_path, M, bank);
  return (index & ((1 << logg_arr[bank]) - 1));
}

uint16_t tage_sc_l::gtag(unsigned int PC, int bank,
                           const dynamic_bitset& global_hist) const
{
  unsigned int ch0_comp = fold_history(global_hist, m[bank], TB[bank]);
  unsigned int ch1_comp = fold_history(global_hist, m[bank], TB[bank] - 1);
  int tag = PC ^ ch0_comp ^ (ch1_comp << 1);
  return (tag & ((1 << TB[bank]) - 1));
}

void tage_sc_l::ctrupdate(int8_t& ctr, bool taken, int nbits) {
  if(taken) {
    if(ctr < ((1 << (nbits - 1)) - 1)) ctr++;
  } else {
    if(ctr > -(1 << (nbits - 1))) ctr--;
  }
}

bool tage_sc_l::getbim() {
  BIM = (btable[BI].pred << 1) + (btable[BI >> HYSTSHIFT].hyst);
  HighConf = (BIM == 0) || (BIM == 3);
  LowConf = !HighConf;
  AltConf = HighConf;
  MedConf = false;
  return (btable[BI].pred > 0);
}

void tage_sc_l::baseupdate(bool Taken) {
  int inter = BIM;
  if(Taken) { if(inter < 3) inter++; }
  else if(inter > 0) inter--;
  btable[BI].pred = inter >> 1;
  btable[BI >> HYSTSHIFT].hyst = (inter & 1);
}

int tage_sc_l::MYRANDOM() {
  Seed++;
  Seed = (Seed >> 21) + (Seed << 11);
  Seed = (Seed >> 10) + (Seed << 22);
  return Seed;
}

// ======================== TAGE PREDICTION ========================

void tage_sc_l::Tagepred(uint64_t PC, const dynamic_bitset& global_hist, long long phist) {
  HitBank = 0;
  AltBank = 0;

  for(int i = 1; i <= NHIST; i += 2) {
    GI[i] = gindex(PC, i, phist, global_hist);
    GTAG[i] = gtag(PC, i, global_hist);
    GTAG[i + 1] = GTAG[i];
    GI[i + 1] = GI[i] ^ (GTAG[i] & ((1 << LOGG) - 1));
  }

  int T = (PC ^ (phist & ((1LL << m[BORN]) - 1))) % NBANKHIGH;
  for(int i = BORN; i <= NHIST; i++)
    if(NOSKIP[i]) {
      GI[i] += (T << LOGG);
      T++; T = T % NBANKHIGH;
    }
  T = (PC ^ (phist & ((1LL << m[1]) - 1))) % NBANKLOW;
  for(int i = 1; i <= BORN - 1; i++)
    if(NOSKIP[i]) {
      GI[i] += (T << LOGG);
      T++; T = T % NBANKLOW;
    }

  BI = (PC ^ (PC >> 2)) & ((1 << LOGB) - 1);

  alttaken = getbim();
  tage_pred = alttaken;
  LongestMatchPred = alttaken;

  // Look for longest matching bank
  for(int i = NHIST; i > 0; i--) {
    if(NOSKIP[i]) {
      gentry* gt = get_gtable(i);
      int idx = GI[i] % ((i >= BORN) ? SizeTable[BORN] : SizeTable[1]);
      if(gt[idx].tag == GTAG[i]) {
        HitBank = i;
        LongestMatchPred = (gt[idx].ctr >= 0);
        break;
      }
    }
  }

  // Look for alternate bank
  for(int i = HitBank - 1; i > 0; i--) {
    if(NOSKIP[i]) {
      gentry* gt = get_gtable(i);
      int idx = GI[i] % ((i >= BORN) ? SizeTable[BORN] : SizeTable[1]);
      if(gt[idx].tag == GTAG[i]) {
        AltBank = i;
        break;
      }
    }
  }

  if(HitBank > 0) {
    gentry* gt_hit = get_gtable(HitBank);
    int hit_idx = GI[HitBank] % ((HitBank >= BORN) ? SizeTable[BORN] : SizeTable[1]);

    if(AltBank > 0) {
      gentry* gt_alt = get_gtable(AltBank);
      int alt_idx = GI[AltBank] % ((AltBank >= BORN) ? SizeTable[BORN] : SizeTable[1]);
      alttaken = (gt_alt[alt_idx].ctr >= 0);
      AltConf = (std::abs(2 * gt_alt[alt_idx].ctr + 1) > 1);
    } else {
      alttaken = getbim();
    }

    int indusealt = ((((HitBank - 1) / 8) << 1) + AltConf) % (SIZEUSEALT - 1);
    bool Huse_alt_on_na = (use_alt_on_na[indusealt] >= 0);
    if(!Huse_alt_on_na || (std::abs(2 * gt_hit[hit_idx].ctr + 1) > 1))
      tage_pred = LongestMatchPred;
    else
      tage_pred = alttaken;

    HighConf = (std::abs(2 * gt_hit[hit_idx].ctr + 1) >= (1 << CWIDTH) - 1);
    LowConf = (std::abs(2 * gt_hit[hit_idx].ctr + 1) == 1);
    MedConf = (std::abs(2 * gt_hit[hit_idx].ctr + 1) == 5);
  }
}

// ======================== LOOP PREDICTOR ========================

bool tage_sc_l::getloop(uint64_t PC) {
  LHIT = -1;
  LI = lindex(PC);
  LIB = ((PC >> (LOGL - 2)) & ((1 << (LOGL - 2)) - 1));
  LTAG = (PC >> (LOGL - 2)) & ((1 << (2 * LOOPTAG)) - 1);
  LTAG ^= (LTAG >> LOOPTAG);
  LTAG = (LTAG & ((1 << LOOPTAG) - 1));

  for(int i = 0; i < 4; i++) {
    int index = (LI ^ ((LIB >> i) << 2)) + i;
    if(index < static_cast<int>(ltable.size()) && ltable[index].TAG == LTAG) {
      LHIT = i;
      LVALID = ((ltable[index].confid == CONFLOOP) ||
                (ltable[index].confid * ltable[index].NbIter > 128));
      if(ltable[index].CurrentIter + 1 == ltable[index].NbIter)
        return !(ltable[index].dir);
      return ltable[index].dir;
    }
  }
  LVALID = false;
  return false;
}

void tage_sc_l::loopupdate(uint64_t PC, bool Taken, bool ALLOC) {
  if(LHIT >= 0) {
    int index = (LI ^ ((LIB >> LHIT) << 2)) + LHIT;
    if(index >= static_cast<int>(ltable.size())) return;

    if(LVALID) {
      if(Taken != predloop) {
        ltable[index].NbIter = 0;
        ltable[index].age = 0;
        ltable[index].confid = 0;
        ltable[index].CurrentIter = 0;
        return;
      } else if((predloop != tage_pred) || ((MYRANDOM() & 7) == 0))
        if(ltable[index].age < CONFLOOP) ltable[index].age++;
    }

    ltable[index].CurrentIter++;
    ltable[index].CurrentIter &= ((1 << WIDTHNBITERLOOP) - 1);

    if(ltable[index].CurrentIter > ltable[index].NbIter) {
      ltable[index].confid = 0;
      ltable[index].NbIter = 0;
    }

    if(Taken != ltable[index].dir) {
      if(ltable[index].CurrentIter == ltable[index].NbIter) {
        if(ltable[index].confid < CONFLOOP) ltable[index].confid++;
        if(ltable[index].NbIter < 3) {
          ltable[index].dir = Taken;
          ltable[index].NbIter = 0;
          ltable[index].age = 0;
          ltable[index].confid = 0;
        }
      } else {
        if(ltable[index].NbIter == 0) {
          ltable[index].confid = 0;
          ltable[index].NbIter = ltable[index].CurrentIter;
        } else {
          ltable[index].NbIter = 0;
          ltable[index].confid = 0;
        }
      }
      ltable[index].CurrentIter = 0;
    }
  } else if(ALLOC) {
    uint64_t X = MYRANDOM() & 3;
    if((MYRANDOM() & 3) == 0)
      for(int i = 0; i < 4; i++) {
        int LHIT_aux = (X + i) & 3;
        int index = (LI ^ ((LIB >> LHIT_aux) << 2)) + LHIT_aux;
        if(index >= static_cast<int>(ltable.size())) continue;
        if(ltable[index].age == 0) {
          ltable[index].dir = !Taken;
          ltable[index].TAG = LTAG;
          ltable[index].NbIter = 0;
          ltable[index].age = 7;
          ltable[index].confid = 0;
          ltable[index].CurrentIter = 0;
          break;
        } else
          ltable[index].age--;
        break;
      }
  }
}

// ======================== SC PREDICTION ========================

// GINDEX macro replacement — compute GEHL index
static inline long long sc_gindex(uint64_t PC, long long bhist, int i, int logs, int NBR) {
  long long index = (static_cast<long long>(PC)) ^ bhist ^ (bhist >> (8 - i))
                    ^ (bhist >> (16 - 2 * i)) ^ (bhist >> (24 - 3 * i))
                    ^ (bhist >> (32 - 3 * i)) ^ (bhist >> (40 - 4 * i));
  return index & ((1 << (logs - (i >= (NBR - 2)))) - 1);
}

int tage_sc_l::Gpredict(uint64_t PC, long long BHIST, int* length,
                         int8_t** tab, int NBR, int logs,
                         int8_t* W, int logsize) const
{
  int PERCSUM = 0;
  for(int i = 0; i < NBR; i++) {
    long long bhist = BHIST & ((1LL << length[i]) - 1);
    long long index = sc_gindex(PC, bhist, i, logs, NBR);
    int8_t ctr = tab[i][index];
    PERCSUM += (2 * ctr + 1);
  }
  unsigned int indupds = INDUPDS_fn(PC);
  PERCSUM = (1 + (W[indupds] >= 0)) * PERCSUM;
  return PERCSUM;
}

void tage_sc_l::Gupdate(uint64_t PC, bool taken, long long BHIST, int* length,
                          int8_t** tab, int NBR, int logs,
                          int8_t* W, int logsize)
{
  int PERCSUM = 0;
  for(int i = 0; i < NBR; i++) {
    long long bhist = BHIST & ((1LL << length[i]) - 1);
    long long index = sc_gindex(PC, bhist, i, logs, NBR);
    PERCSUM += (2 * tab[i][index] + 1);
    ctrupdate(tab[i][index], taken, PERCWIDTH);
  }

  unsigned int indupds = INDUPDS_fn(PC);
  int XSUM = LSUM - ((W[indupds] >= 0)) * PERCSUM;
  if((XSUM + PERCSUM >= 0) != (XSUM >= 0))
    ctrupdate(W[indupds], ((PERCSUM >= 0) == taken), EWIDTH);
}

// ======================== FULL PREDICTION ========================

bool tage_sc_l::GetPrediction(uint64_t PC,
                               const dynamic_bitset& global_hist,
                               const dynamic_bitset& local_hist,
                               const bp_context& ctx)
{
  Tagepred(PC, global_hist, ctx.phist);
  pred_taken = tage_pred;

  // Loop predictor
  predloop = getloop(PC);
  pred_taken = ((WITHLOOP >= 0) && LVALID) ? predloop : pred_taken;

  pred_inter = pred_taken;

  // SC
  LSUM = 0;
  unsigned int indupds = INDUPDS_fn(PC);

  // BIAS tables
  unsigned int indbias = ((((PC ^ (PC >> 2)) << 1) ^ (LowConf & (LongestMatchPred != alttaken))) << 1) + pred_inter;
  indbias &= ((1 << LOGBIAS) - 1);
  int8_t ctr = Bias[indbias];
  LSUM += (2 * ctr + 1);

  unsigned int indbiassk = ((((PC ^ (PC >> (LOGBIAS - 2))) << 1) ^ HighConf) << 1) + pred_inter;
  indbiassk &= ((1 << LOGBIAS) - 1);
  ctr = BiasSK[indbiassk];
  LSUM += (2 * ctr + 1);

  unsigned int indbiasbank = (pred_inter + (((HitBank + 1) / 4) << 4) + (HighConf << 1) + (LowConf << 2) + ((AltBank != 0) << 3) + ((PC ^ (PC >> 2)) << 7));
  indbiasbank &= ((1 << LOGBIAS) - 1);
  ctr = BiasBank[indbiasbank];
  LSUM += (2 * ctr + 1);

  LSUM = (1 + (WB[indupds] >= 0)) * LSUM;

  // GEHL components — derive from provided global_hist / local_hist
  long long ghist_ll = static_cast<long long>(fold_bitset_to_val(global_hist, 64));
  long long lhist_ll = static_cast<long long>(fold_bitset_to_val(local_hist, 64));
  LSUM += Gpredict((PC << 1) + pred_inter, ghist_ll, Gm, GGEHL, GNB, LOGGNB, WG, LOGSIZEUPS);
  LSUM += Gpredict(PC, ctx.phist, Pm, PGEHL, PNB, LOGPNB, WP, LOGSIZEUPS);
  LSUM += Gpredict(PC, lhist_ll, Lm, LGEHL, LNB, LOGLNB, WL, LOGSIZEUPS);
  LSUM += Gpredict(PC, lhist_ll ^ static_cast<long long>(PC & 15), Sm, SGEHL, SNB, LOGSNB, WS, LOGSIZEUPS);
  LSUM += Gpredict(PC, lhist_ll, Tm, TGEHL, TNB, LOGTNB, WT, LOGSIZEUPS);

  bool SCPRED = (LSUM >= 0);

  THRES = (updatethreshold >> 3) + Pupdatethreshold[INDUPD_fn(PC)]
          + 12 * ((WB[indupds] >= 0) + (WP[indupds] >= 0) +
                  (WS[indupds] >= 0) + (WT[indupds] >= 0) + (WL[indupds] >= 0) +
                  (WG[indupds] >= 0));

  if(pred_inter != SCPRED) {
    pred_taken = SCPRED;
    if(HighConf) {
      if(std::abs(LSUM) < THRES / 4)
        pred_taken = pred_inter;
      else if(std::abs(LSUM) < THRES / 2)
        pred_taken = (SecondH < 0) ? SCPRED : pred_inter;
    }
    if(MedConf)
      if(std::abs(LSUM) < THRES / 4)
        pred_taken = (FirstH < 0) ? SCPRED : pred_inter;
  }

  return pred_taken;
}

// ======================== FULL UPDATE ========================

void tage_sc_l::UpdatePredictor(uint64_t PC, bool resolveDir, bool predDir,
                                 const dynamic_bitset& global_hist,
                                 const dynamic_bitset& local_hist,
                                 const bp_context& ctx)
{
  // Loop predictor update
  if(LVALID) {
    if(pred_taken != predloop)
      ctrupdate(WITHLOOP, (predloop == resolveDir), 7);
  }
  loopupdate(PC, resolveDir, (pred_taken != resolveDir));

  // SC update
  bool SCPRED = (LSUM >= 0);
  unsigned int indupds = INDUPDS_fn(PC);

  if(pred_inter != SCPRED) {
    if(std::abs(LSUM) < THRES) {
      if(HighConf && std::abs(LSUM) < THRES / 2 && std::abs(LSUM) >= THRES / 4)
        ctrupdate(SecondH, (pred_inter == resolveDir), CONFWIDTH);
      if(MedConf && std::abs(LSUM) < THRES / 4)
        ctrupdate(FirstH, (pred_inter == resolveDir), CONFWIDTH);
    }
  }

  if((SCPRED != resolveDir) || (std::abs(LSUM) < THRES)) {
    if(SCPRED != resolveDir) {
      Pupdatethreshold[INDUPD_fn(PC)] += 1;
      updatethreshold += 1;
    } else {
      Pupdatethreshold[INDUPD_fn(PC)] -= 1;
      updatethreshold -= 1;
    }

    int widthresp_half = (1 << (WIDTHRESP - 1));
    if(Pupdatethreshold[INDUPD_fn(PC)] >= widthresp_half)
      Pupdatethreshold[INDUPD_fn(PC)] = widthresp_half - 1;
    if(Pupdatethreshold[INDUPD_fn(PC)] < -widthresp_half)
      Pupdatethreshold[INDUPD_fn(PC)] = -widthresp_half;

    int widthres_half = (1 << (WIDTHRES - 1));
    if(updatethreshold >= widthres_half)
      updatethreshold = widthres_half - 1;
    if(updatethreshold < -widthres_half)
      updatethreshold = -widthres_half;

    // Update BIAS tables
    unsigned int indbias = ((((PC ^ (PC >> 2)) << 1) ^ (LowConf & (LongestMatchPred != alttaken))) << 1) + pred_inter;
    indbias &= ((1 << LOGBIAS) - 1);
    unsigned int indbiassk = ((((PC ^ (PC >> (LOGBIAS - 2))) << 1) ^ HighConf) << 1) + pred_inter;
    indbiassk &= ((1 << LOGBIAS) - 1);
    unsigned int indbiasbank = (pred_inter + (((HitBank + 1) / 4) << 4) + (HighConf << 1) + (LowConf << 2) + ((AltBank != 0) << 3) + ((PC ^ (PC >> 2)) << 7));
    indbiasbank &= ((1 << LOGBIAS) - 1);

    // WB update
    int XSUM = LSUM - ((WB[indupds] >= 0) * ((2 * Bias[indbias] + 1) + (2 * BiasSK[indbiassk] + 1) + (2 * BiasBank[indbiasbank] + 1)));
    if((XSUM + ((2 * Bias[indbias] + 1) + (2 * BiasSK[indbiassk] + 1) + (2 * BiasBank[indbiasbank] + 1)) >= 0) != (XSUM >= 0))
      ctrupdate(WB[indupds], (((2 * Bias[indbias] + 1) + (2 * BiasSK[indbiassk] + 1) + (2 * BiasBank[indbiasbank] + 1) >= 0) == resolveDir), EWIDTH);

    ctrupdate(Bias[indbias], resolveDir, PERCWIDTH);
    ctrupdate(BiasSK[indbiassk], resolveDir, PERCWIDTH);
    ctrupdate(BiasBank[indbiasbank], resolveDir, PERCWIDTH);

    long long ghist_ll = static_cast<long long>(fold_bitset_to_val(global_hist, 64));
    long long lhist_ll = static_cast<long long>(fold_bitset_to_val(local_hist, 64));
    Gupdate((PC << 1) + pred_inter, resolveDir, ghist_ll, Gm, GGEHL, GNB, LOGGNB, WG, LOGSIZEUPS);
    Gupdate(PC, resolveDir, ctx.phist, Pm, PGEHL, PNB, LOGPNB, WP, LOGSIZEUPS);
    Gupdate(PC, resolveDir, lhist_ll, Lm, LGEHL, LNB, LOGLNB, WL, LOGSIZEUPS);
    Gupdate(PC, resolveDir, lhist_ll ^ static_cast<long long>(PC & 15), Sm, SGEHL, SNB, LOGSNB, WS, LOGSIZEUPS);
    Gupdate(PC, resolveDir, lhist_ll, Tm, TGEHL, TNB, LOGTNB, WT, LOGSIZEUPS);
  }

  // TAGE update
  bool ALLOC = ((tage_pred != resolveDir) & (HitBank < NHIST));

  if(HitBank > 0) {
    gentry* gt_hit = get_gtable(HitBank);
    int hit_size = (HitBank >= BORN) ? SizeTable[BORN] : SizeTable[1];
    int hit_idx = GI[HitBank] % hit_size;

    bool PseudoNewAlloc = (std::abs(2 * gt_hit[hit_idx].ctr + 1) <= 1);
    if(PseudoNewAlloc) {
      if(LongestMatchPred == resolveDir) ALLOC = false;
      if(LongestMatchPred != alttaken) {
        int indusealt = ((((HitBank - 1) / 8) << 1) + AltConf) % (SIZEUSEALT - 1);
        ctrupdate(use_alt_on_na[indusealt], (alttaken == resolveDir), ALTWIDTH);
      }
    }
  }

  if(pred_taken == resolveDir)
    if((MYRANDOM() & 31) != 0) ALLOC = false;

  if(ALLOC) {
    int T = NNN;
    int A = 1;
    if((MYRANDOM() & 127) < 32) A = 2;
    int Penalty = 0;
    int NA = 0;
    int DEP = ((((HitBank - 1 + 2 * A) & 0xffe)) ^ (MYRANDOM() & 1));

    for(int I = DEP; I < NHIST; I += 2) {
      int i = I + 1;
      if(i > NHIST) break;
      bool Done = false;
      if(NOSKIP[i]) {
        gentry* gt = get_gtable(i);
        int tsize = (i >= BORN) ? SizeTable[BORN] : SizeTable[1];
        int idx = GI[i] % tsize;
        if(gt[idx].u == 0) {
          if(std::abs(2 * gt[idx].ctr + 1) <= 3) {
            gt[idx].tag = GTAG[i];
            gt[idx].ctr = resolveDir ? 0 : -1;
            NA++;
            if(T <= 0) break;
            I += 2; Done = true; T -= 1;
          } else {
            if(gt[idx].ctr > 0) gt[idx].ctr--;
            else gt[idx].ctr++;
          }
        } else {
          Penalty++;
        }
      }

      if(!Done) {
        i = (I ^ 1) + 1;
        if(i <= NHIST && NOSKIP[i]) {
          gentry* gt = get_gtable(i);
          int tsize = (i >= BORN) ? SizeTable[BORN] : SizeTable[1];
          int idx = GI[i] % tsize;
          if(gt[idx].u == 0) {
            if(std::abs(2 * gt[idx].ctr + 1) <= 3) {
              gt[idx].tag = GTAG[i];
              gt[idx].ctr = resolveDir ? 0 : -1;
              NA++;
              if(T <= 0) break;
              I += 2; T -= 1;
            } else {
              if(gt[idx].ctr > 0) gt[idx].ctr--;
              else gt[idx].ctr++;
            }
          } else {
            Penalty++;
          }
        }
      }
    }

    TICK += (Penalty - 2 * NA);
    if(TICK < 0) TICK = 0;
    if(TICK >= BORNTICK) {
      // Reset usefulness bits for low and high bank tables
      for(int j = 0; j < SizeTable[1]; j++) gtable_low[j].u >>= 1;
      for(int j = 0; j < SizeTable[BORN]; j++) gtable_high[j].u >>= 1;
      TICK = 0;
    }
  }

  // Update hit bank predictions
  if(HitBank > 0) {
    gentry* gt_hit = get_gtable(HitBank);
    int hit_size = (HitBank >= BORN) ? SizeTable[BORN] : SizeTable[1];
    int hit_idx = GI[HitBank] % hit_size;

    if(std::abs(2 * gt_hit[hit_idx].ctr + 1) == 1)
      if(LongestMatchPred != resolveDir) {
        if(AltBank > 0) {
          gentry* gt_alt = get_gtable(AltBank);
          int alt_size = (AltBank >= BORN) ? SizeTable[BORN] : SizeTable[1];
          int alt_idx = GI[AltBank] % alt_size;
          ctrupdate(gt_alt[alt_idx].ctr, resolveDir, CWIDTH);
        }
        if(AltBank == 0) baseupdate(resolveDir);
      }
    ctrupdate(gt_hit[hit_idx].ctr, resolveDir, CWIDTH);
    if(std::abs(2 * gt_hit[hit_idx].ctr + 1) == 1)
      gt_hit[hit_idx].u = 0;

    if(alttaken == resolveDir)
      if(AltBank > 0) {
        gentry* gt_alt = get_gtable(AltBank);
        int alt_size = (AltBank >= BORN) ? SizeTable[BORN] : SizeTable[1];
        int alt_idx = GI[AltBank] % alt_size;
        if(std::abs(2 * gt_alt[alt_idx].ctr + 1) == 7)
          if(gt_hit[hit_idx].u == 1)
            if(LongestMatchPred == resolveDir)
              gt_hit[hit_idx].u = 0;
      }
  } else {
    baseupdate(resolveDir);
  }

  if(LongestMatchPred != alttaken)
    if(LongestMatchPred == resolveDir) {
      gentry* gt_hit = get_gtable(HitBank);
      int hit_size = (HitBank >= BORN) ? SizeTable[BORN] : SizeTable[1];
      int hit_idx = GI[HitBank] % hit_size;
      if(gt_hit[hit_idx].u < (1 << UWIDTH) - 1)
        gt_hit[hit_idx].u++;
    }

  // Streaming history updates (phist, GHIST_internal, L_shist, S_slhist, T_slhist)
  // are now handled by SPPAM via bp_context::update().
}

// ======================== INTERFACE ========================

std::pair<bool,double> tage_sc_l::predict_branch(champsim::address ip,
                                                   const dynamic_bitset& global_hist,
                                                   const dynamic_bitset& local_hist,
                                                   const bp_context& ctx)
{
  uint64_t pc = ip.to<uint64_t>();
  bool prediction = GetPrediction(pc, global_hist, local_hist, ctx);
  last_pred = prediction;

  if(prediction)
    predict_taken_count++;
  else
    predict_nottaken_count++;

  // Confidence based on TAGE + SC
  double conf = 0.0;
  if(HitBank > 0) {
    gentry* gt_hit = get_gtable(HitBank);
    int hit_size = (HitBank >= BORN) ? SizeTable[BORN] : SizeTable[1];
    int hit_idx = GI[HitBank] % hit_size;
    conf = static_cast<double>(std::abs(2 * gt_hit[hit_idx].ctr + 1)) / ((1 << CWIDTH) - 1);
  } else {
    conf = HighConf ? 1.0 : 0.5;
  }

  if(DEBUG)
    fmt::print("[TAGE_SC_L] PREDICT IP: {} PRED: {} CONF: {:.3f} HitBank: {} LSUM: {}\n",
               ip, prediction ? "taken" : "not taken", conf, HitBank, LSUM);

  return {prediction, conf};
}

void tage_sc_l::last_branch_result(champsim::address ip,
                                    const dynamic_bitset& global_hist,
                                    const dynamic_bitset& local_hist,
                                    bool taken,
                                    bp_context& ctx)
{
  if(taken)
    outcome_taken_count++;
  else
    outcome_nottaken_count++;

  uint64_t pc = ip.to<uint64_t>();
  UpdatePredictor(pc, taken, last_pred, global_hist, local_hist, ctx);

  // Context update (phist, SC histories, path, recency) done by SPPAM via ctx.update()

  if(DEBUG)
    fmt::print("[TAGE_SC_L] OUTCOME IP: {} TAKEN: {}\n", ip, taken);
}

void tage_sc_l::print_heartbeat() {
  fmt::print("[TAGE_SC_L] Predicted Taken: {} Predicted Not-taken: {}\n",
             predict_taken_count, predict_nottaken_count);
  fmt::print("[TAGE_SC_L] Outcome Taken: {} Outcome Not-taken: {}\n",
             outcome_taken_count, outcome_nottaken_count);
  predict_taken_count = 0;
  predict_nottaken_count = 0;
  outcome_taken_count = 0;
  outcome_nottaken_count = 0;
}

void tage_sc_l::print_stats() {
  std::ofstream stats_file;
  stats_file.open("sppam_b_predict_tage_sc_l.txt", std::ios::out | std::ios::trunc);

  // Print bimodal table utilization
  int bused = 0;
  for(auto& b : btable) if(b.pred != 0 || b.hyst != 1) bused++;
  stats_file << fmt::format("BIMODAL: {}/{} entries used\n", bused, btable.size());

  // Print tagged table utilization
  int low_used = 0, high_used = 0;
  for(auto& g : gtable_low) if(g.tag != 0) low_used++;
  for(auto& g : gtable_high) if(g.tag != 0) high_used++;
  stats_file << fmt::format("TAGGED_LOW: {}/{} entries used\n", low_used, gtable_low.size());
  stats_file << fmt::format("TAGGED_HIGH: {}/{} entries used\n", high_used, gtable_high.size());

  stats_file << fmt::format("UPDATE_THRESHOLD: {}\n", updatethreshold);
  stats_file.close();
}

}
