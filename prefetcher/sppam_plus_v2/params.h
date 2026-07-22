// SPPAM design-space parameters. Every knob the original SPPAM hard-coded as a
// constexpr is exposed here as a runtime field (defaults match the original),
// grouped by the design axes under exploration. A JSON object overrides any
// subset of fields by name.
#ifndef SPPAM_DSE_PARAMS_H
#define SPPAM_DSE_PARAMS_H

#include <array>
#include <cstdint>
#include <string>

#include "dram_model.h"
#include <nlohmann/json.hpp>

namespace sppam_dse
{

struct params {
  std::string name = "default"; // label for this configuration in the output

  // --- Region size ---
  // Defaults sized for a buildable (~32 KiB) L2 prefetcher; see state_kib().
  uint64_t page_bits = 12;     // 4 KiB physical page; a HARD prefetch-squash boundary
  uint64_t region_bits = 11;   // region-table-entry granularity (2 KiB; <= page_bits)
  std::size_t region_sets = 64;
  std::size_t region_ways = 12;
  std::size_t llc_region_sets = 32; // shadow LLC-residency map
  std::size_t llc_region_ways = 8;
  bool region_hash = false;    // XOR-fold the page number into the region set index
  // Region/LLC/CPT tags fold the long region vpn to a hashed short tag. False-match
  // prob ~= ways/2^region_tag_bits (16 bits, 12 ways -> ~0.018%; negligible).
  uint32_t region_tag_bits = 16;
  // Index the region set by the PAGE (sub-regions of a page share a set, held in
  // its ways) so within-page neighbors are always co-resident for shadow/SPP
  // reconstruction. Lets smaller regions work without scattering a page's pieces.
  bool region_page_aligned_sets = true;
  // Stitch shadow context from the adjacent sub-page region (region_num +/- 1 in
  // the same 4 KB page), computed from the address (no stored next/prev links).
  // Lets SPPAM learn patterns that cross the 2 KB region boundary within a page.
  bool within_page_shadow = true;
  // The original region entry stores next/prev shadow-page links (~2*vpn bits).
  // They are inert in this physical-address, berti-less model (never set), so by
  // default we do NOT charge them to the state budget. Set true for the original
  // (heavier) accounting.
  bool account_shadow_links = false;
  bool dedup_analysis = false; // measure region-bitmap dedup potential (prints to stderr)

  // --- Hybrid combination of SPPAM + SPP ---
  // Shared shadow cache: squash a prefetch (from either engine) to a block already
  // resident in L2 (the prefetch map = L2 residency, cleared on eviction).
  bool enable_shadow_squash = true;
  bool exact_shadow_test = false;   // TEST ONLY: route the residency filter through a leak-free fill/evict mirror (unbuildable state; measures the coverage lost to stale prefetch-map bits)
  // Hybrid residency filter: on region eviction, spill the region's still-resident blocks into a
  // rolling-clock 1-bit bloom, so residency of evicted (typically sparse/fragmented) regions is not
  // lost. shadow_resident: region-in-table -> exact prefetch_map; region-absent -> bloom. Cheap on
  // dense workloads (few evictions, bloom idle), recovers fragmented residency the region table drops.
  // Per-trigger-IP accuracy filter: throttle prefetch triggers whose historical usefulness is low
  // (graph-walk/pointer-chase loads) while sparing predictable streams (validated: datacenter traces
  // drop ~45-61% useless at ~2-5% useful loss). Never fully gates (1/trickle keeps feedback live).
  bool enable_ip_filter = false;
  int ip_filter_threshold = 20;        // SOFT: usefulness below this -> LIGHT throttle (1/ip_filter_trickle)
  uint32_t ip_filter_min_samples = 8;  // resolved prefetches before judging an IP (low: correct ratio via
  int ip_filter_trickle = 6;           // LIGHT throttle divisor (mid-usefulness IPs: issue 1/N) [swept]
  int ip_filter_threshold_hard = 8;    // HARD: usefulness below this -> HARSH throttle (1/trickle_hard) [swept]
  bool ip_filter_use_pe = false;       // metric: false=usefulness (used/(used+useless)); true=per-IP PE
                                       // (I_UPF-I_POLL-I_LAT) -- captures pollution, spares coverage-limited IPs
  double ip_pe_hard_frac = 0.5;        // PE metric: per-pf PE below -frac*avg_lat -> harsh throttle
  // Per-IP per-PHASE PE throttle (the UNCONFOUNDED metric): throttle an IP by the fraction of its ACTIVE
  // phases in which its prefetches are net-harmful (per-phase PE<0). Unlike the per-pf PE AVERAGE (use_pe),
  // this keeps temporal variance -- an IP net-useful on average but harmful in most windows is caught.
  // Self-regulating per IP -> no separate global keep/disable signal. Needs enable_ip_filter.
  bool ip_filter_use_pe_phase = false;
  uint32_t ip_pe_phase_soft = 30;      // harm-fraction %% below this -> full issue
  uint32_t ip_pe_phase_hard = 50;      // harm-fraction %% at/above this -> harsh throttle (ip_filter_trickle_hard)
  double ip_pe_phase_margin = 0.0;     // magnitude guard: spare unless avg-PE < -margin*avg_lat (0 = sign only).
                                       // Separates deeply-harmful IPs (xalan) from marginally-negative-but-
                                       // IPC-helping IPs (datacenter) that PE mis-scores (0.85 IPC corr, not 1).
  // DEPTH throttle (not volume): as an IP's usefulness degrades, cap its lookahead DEPTH -> prefetch shallower.
  // A low-usefulness IP whose misses are UNTIMELY (right addr, too early: mcf/triangle) self-corrects -- shallow
  // prefetches land in time -> useful -> cap relaxes. A truly-bad IP (xalan/pollution) issues fewer, nearer
  // (less-polluting) blocks. Uses the SAME usefulness bands as the volume trickle. Replaces the volume trickle.
  bool ip_filter_depth_throttle = false;
  int ip_depth_mid = 2;                // mid-usefulness IPs: cap lookaheads at this depth
  int ip_depth_min = 0;                // near-dead IPs: shallowest (0 = direct pattern only, no lookahead)
  // Action selection by WHY an IP is low-usefulness: UNTIMELY (right addr, evicted-before-use) -> DEPTH cap
  // (shallower lands in time -> useful: mcf/triangle). TRULY-BAD (wrong addr) -> VOLUME trickle (drop the
  // wrong prefetches: xalan/datacenter). Depth cap on a truly-bad IP just strips good deep coverage (hurts).
  uint32_t ip_untimely_thresh = 50;    // %% of an IP's useless prefetches that are untimely -> use depth (else volume)
  // Cache-STRESS gate: depth (timeliness) only helps when the cache RETAINS a timely prefetch until use.
  // At low L2 hit rate (thrashing pointer-chase: bc/yankee) a shallow prefetch is still evicted before use,
  // so shallowing buys nothing and strips MLP -> regress. Gate depth-throttle on L2 demand hit rate >= this.
  double ip_depth_hitrate_min = 0.72;
  // MLP gate (complements the stress gate): if the UPSTREAM (demand+berti) MSHR occupancy is high, the
  // PROGRAM is already parallel -- our deep prefetches ride real inherent MLP, so shortening them strips
  // parallelism (bc/cc-5/cc-13/sssp regress). Only depth-throttle when upstream MLP is LOW (serial program:
  // mcf/triangle, where deep-untimely prefetches are waste). Catches the high-hit-rate losers the stress
  // gate lets through (cc-5/cc-13 @73%). 0 = off.
  double ip_depth_mlp_max = 0.6;
  bool ip_filter_pe_veto = false;      // HYBRID: usefulness throttles, but SPARE an IP whose per-pf PE is
                                       // clearly positive (> ip_pe_veto_frac*avg_lat) -- rescues coverage-
                                       // limited IPs (sierra) that usefulness wrongly throttles.
  double ip_pe_veto_frac = 0.25;       // veto threshold as a fraction of mean fill latency
  int ip_filter_trickle_hard = 24;     // HARSH throttle divisor (near-dead IPs: issue 1/N) [swept]. Graded: harsher
                                       // at high uselessness, lighter at mid usefulness.
  uint32_t ip_filter_age_shift = 20;   // halve the per-IP counters every 2^shift issues (adaptivity)
  int ip_filter_max_useful_loss = 100; // auto-backoff budget (OFF by default: useful-loss does NOT predict
                                       // AMAT -- cc5 is pollution-limited so throttling HELPS it; sierra is
                                       // coverage-limited and the estimate cannot catch it. Proper gate is
                                       // pollution-based (PE I_POLL), TODO. 100 = never disables.
  // IP-filter table geometry (DSE-swept). The historical 4096 x uint32 impl was a ~16x over-provision:
  // a coarse per-IP throttle needs far fewer buckets, and the counters saturate + decay (ip_filter_age_shift)
  // so a byte suffices. iphash() masks to ip_table_entries; every per-IP array (useful/useless/gate + optional
  // ev/untimely + optional bwd) is ip_table_entries x ip_ctr_bits.
  uint32_t ip_table_entries = 4096;    // per-trigger-IP bucket count (power of 2)
  uint32_t ip_ctr_bits = 8;            // saturating width of each per-IP counter
  uint32_t evicted_unused_cap = 2048;  // bound on the depth-throttle re-demand watch list (block->IP)
  uint32_t ip_sample_div = 4;          // sample 1/N issued prefetches into the block->IP attribution table
  uint64_t ip_track_timeout = 50000;   // cycles: a pinned in-flight sample older than this is stale ->
                                       // reclaimed and counted USELESS (sppam_b USELESS_ON_TIMEOUT), so
                                       // long-lived useless prefetches turn over instead of hogging slots
  bool enable_resid_bloom = false; // VALIDATED but OFF: cuts fragmented redundancy 35-40% (~5 KiB), but that
                                   // redundancy is BW-FREE (redundant pf hits L2, never reaches LLC/DRAM), so
                                   // no AMAT payoff, while the bloom's FP costs a little coverage -> net AMAT
                                   // flat-to-slightly-worse. Keep as a knob for a tag-bandwidth-bound regime.
  uint32_t resid_bloom_bits = 40960; // ~5 KiB
  int resid_bloom_k = 2;             // hashes per block
  int resid_bloom_clear = 4;         // bits cleared per spill (rolling decay); ~2*k -> ~33% occupancy
  // ACCESS-map bloom (distinct from the resid/prefetch bloom above): a SHARED backing store for the
  // per-region access maps -- one bloom filter per within-region offset, indexed by hash(region ^ off),
  // inserting k hashes of the region id. When a thrashed-out region is re-allocated, its access map is
  // WARM-SEEDED from the bloom (reconstruct all offsets) instead of cold-starting -> retains patterns the
  // fixed region table capacity-evicts. Gradual clearing: a per-offset cache-eviction counter clears a
  // slice of that offset's filter once evictions mapping to it exceed am_bloom_clear_thresh (keeps it fresh).
  bool enable_am_bloom = false;
  int am_bloom_size = 2048;          // bits per per-offset filter (state = blocks_per_region * this)
  int am_bloom_k = 4;                // hashes per region id
  int am_bloom_clear_thresh = 64;    // per-offset cache evictions before clearing a slice
  int am_bloom_clear_frac = 8;       // clear size/frac bits per trigger (rotating hand)
  // Per-trigger bidding: each engine bids on a trigger; the highest bidder's
  // prefetches issue, the loser's are dropped (only one wins per trigger).
  bool enable_hybrid_bidding = false;
  bool bid_by_value = false;     // bid = SUM of per-prefetch benefit (value); else MAX (peak confidence)
  uint64_t bid_explore_div = 8;  // every Nth trigger, hand control to the loser so it keeps learning
                                 // (never permanently gate a prefetcher)
  // Fall-through (alternative to bidding): SPPAM is primary; SPP only acts on a
  // trigger when SPPAM produced no prediction (it "didn't see a pattern"). Pure
  // coverage extension -- SPP can never contend a trigger SPPAM already owns.
  bool enable_fallthrough = true;
  uint64_t fallthrough_explore_div = 16; // every Nth SPP candidate, let it through even when
                                         // SPPAM fired, so SPP keeps learning (never permanently gated)

  // --- SPP-lite delta prefetcher (page-keyed, runs alongside SPPAM) ---
  bool enable_spp = false;
  std::size_t spp_st_entries = 256;   // signature-table entries (per-page)
  uint32_t spp_sig_bits = 12;         // signature width (specificity of the rolling hash)
  // Pattern table: a HASHED, tagged, set-associative table (the signature space
  // is sparse, so we hash the signature into a fixed-size table rather than
  // direct-index 2^sig_bits). Size is independent of sig_bits.
  std::size_t spp_pt_sets = 256;
  std::size_t spp_pt_ways = 4;
  std::size_t spp_deltas_per_sig = 4; // delta candidates stored per signature
  uint32_t spp_conf_bits = 7;         // PT confidence resolution (10-bit was excessive)
  uint32_t spp_lookahead = 16;        // max signature-path prefetch depth
  double spp_threshold = 0.25;        // min path confidence to keep prefetching
  // --- GHR / large-delta SPP (specialize SPP to catch cactus-style large strides SPPAM can't) ---
  // A cross-page Global History Register: at a page boundary the lookahead records {signature,
  // landing offset in the next page}; when a new page's first access matches a landing, its
  // signature is SEEDED (warm-start) so SPP follows the stride immediately instead of cold-starting
  // (with a large stride you get only ~page/stride accesses per page -> never build a signature).
  bool spp_ghr = false;               // enable the cross-page GHR (off = classic page-squash SPP)
  std::size_t spp_ghr_entries = 16;   // GHR depth (recent page-cross continuations)
  int spp_min_delta = 0;              // large-delta filter: only train/predict |delta| >= this (0=off)
  // Absolute per-delta confidence gating (SPP's real mechanism): a delta prefetches only if its
  // 0..(2^spp_conf_bits-1) counter >= spp_min_conf; if >=2 deltas clear it (no standout) the pattern
  // is complex -> throttle. 0 = keep the legacy ratio-vs-spp_threshold path gate instead.
  int spp_min_conf = 0;               // absolute confidence threshold (e.g. 48-64 for 7-bit); 0=legacy
  bool spp_multi_high_throttle = true; // silence signatures with >=2 deltas above spp_min_conf
  // SPP usefulness feedback: track whether SPP's prefetches get used vs evicted
  // unused, and scale its lookahead depth by that rate so it stops wasting DRAM
  // bandwidth where it is inaccurate (the speculative tail is the waste source).
  // The SPP efficiency throttle (replaces retired PE management). The lookahead's
  // OWN gate (path confidence < threshold) keys on delta-PREDICTABILITY, which
  // stays high on irregular workloads (mcf) even as usefulness collapses with
  // depth (acc 0.32->0.21, volume 0.34->1.09 pf/dem) -- so deep speculation
  // pollutes. This scales lookahead depth by measured prefetch usefulness instead,
  // killing the wasteful tail where SPP is inaccurate (mcf 0.456->0.403) while
  // leaving accurate workloads at full depth (charlie). See [[sppam-dse-findings]].
  // TODO: per-signature usefulness (the PT is already per-signature) would be
  // surgical on MIXED workloads where a global EMA under/over-throttles.
  bool spp_usefulness_feedback = true;
  // Per-signature usefulness (surgical version of the above): each PT signature
  // carries its own used/seen counters. The lookahead extends past the first step
  // ONLY through signatures that have proven useful (optimistic for unseen sigs ->
  // never gate a new path; a 1/explore_div trickle keeps proven-bad sigs learning).
  // Cuts the path exactly where its signatures go bad, instead of a single global
  // depth cut. Attribution: a small block->sig prefetch filter routes the cache's
  // use/evict feedback back to the originating signature.
  bool spp_per_sig_usefulness = false;
  // State cost is small and matches the natural mental model: one used/seen counter
  // pair PER PT ENTRY (the pattern), plus a SMALL SAMPLED block->sig filter that
  // tracks 1/sample_div of in-flight prefetches to resolve their usefulness. The
  // ratio is what matters, so a sample suffices -> the filter is hundreds of entries.
  std::size_t spp_pf_filter_entries = 512;   // sampled block->sig attribution filter (direct-mapped)
  uint64_t spp_pf_sample_div = 8;            // track 1/N issued prefetches (probabilistic sampling)
  uint32_t spp_usefulness_bits = 5;          // per-pattern used/seen counter resolution (5b -> cap 31)
  double spp_per_sig_floor = 0.35;           // usefulness mapped to zero lookahead depth at/below this
  double spp_per_sig_prior = 12.0;           // pseudocount: blend per-pattern ratio toward the global EMA.
                                             // k=12 matches global on uniformly-bad mcf yet keeps
                                             // per-pattern discrimination on mixed workloads.
  // Share the region table for SPP's signature table: instead of a separate ST,
  // fold {last_offset, signature} into each region entry (region granularity).
  // Saves the ST's tags/structure; costs those fields on every region entry.
  bool spp_share_region_table = false;

  // --- Pattern resolution ---
  uint32_t pattern_size = 6;       // bits per access/prediction pattern
  uint32_t min_pattern_size = 6;   // smallest pattern size scanned (>=, halving)
  // Context-augmented pattern key: concatenate pattern_context_bits of per-region
  // context above the spatial pattern, growing the pattern table 2^context_bits so
  // the same spatial pattern can predict differently by context. Risk: atomization
  // (too many unique keys -> nothing recurs). src 0=off, 1=prev-page history (a
  // rolling hash of pages reached before this one), 2=in-region delta signature.
  uint32_t pattern_context_bits = 0;
  // PC-contextualized pattern table (prototype): fold this many bits of the trigger block's PC hash
  // into the pattern-table KEY (above the spatial pattern + region context). A raw 6-bit pattern can
  // only index 64 sets; adding PC bits widens the key space so the SAME spatial pattern from different
  // PCs learns different predictions (gives SPPAM IP correlation). Needs pattern_table_sets/ways bumped
  // to hold the widened keyspace; state grows ~2^pattern_pc_bits. 0 = off (no PC in the pattern key).
  uint32_t pattern_pc_bits = 4; // PROMOTED default: PC in the pattern key (small accuracy win; needs the bigger PHT below)
  uint32_t pattern_context_src = 0;
  // DEFAULT = contrastive per-block COUNTER mode: real-ChampSim AMAT geomean 0.8349 vs the old
  // positive-only conf-table's 0.8434 (~1% better, HW-free). The counter's +up on accessed /
  // -down on unaccessed blocks is the footprint's true-negative contrast (surfaced by porting
  // Funnel's contrastive learning). See sppam-dse memory. (online_learning below is an
  // alternative per-access trainer -- more pf-efficient but ~0.15% worse AMAT -- default off.)
  bool table_or_counter = true;    // false = conf-table prediction, true = per-bit counters
  uint64_t min_confidence_to_prefetch = 4;
  uint64_t counter_up = 2;         // counter-mode increment
  uint64_t counter_down = 1;       // counter-mode decrement
  // Online (Funnel-style) learning: instead of harvesting the finalized footprint at page
  // death (scrape), train the pattern table IMMEDIATELY on each demand access -- the prior
  // context is the access map as it stands before the access is marked. The just-accessed
  // block is a confirmed positive for the signatures that predict it; one sampled unaccessed
  // prediction bit is a contrastive negative (SGNS-style, avoids the premature-negative that
  // waiting would fix). Removes all scrape-timing sensitivity. Forces counter mode.
  bool online_learning = true; // DEFAULT: online per-access contrastive learning (Funnel-style), scrape off
                               // below. Simpler (no scrape-timing heuristics), leaner, slight AMAT win
                               // (geomean 82.29 vs 82.84). counter RULE unchanged (+counter_up/-counter_down).
  int online_neg_samples = 1;      // contrastive negatives per confirmed positive
  // Per-entry perceptron prediction: keep the table keyed by the short 6-bit spatial signature
  // (no atomization) but predict each block with a small perceptron over pp_hist_bits of the
  // delta/page history (pat_ctx). The perceptron LEARNS whether to use those bits, so deep
  // history informs the prediction without entering the key (which atomized before). pp_hist_bits
  // is the depth = state dial: H=0 reproduces plain counter mode (state-neutral). Requires
  // online_learning + counter mode; set pattern_context_src (1/2) so pat_ctx is populated,
  // pattern_context_bits=0 (history is an input, not a key).
  bool pattern_perceptron = false;
  int pp_hist_bits = 0;            // perceptron SPATIAL history depth (extra access-map bits); 0 = none
  int pp_pc_bits = 0;              // perceptron PC-hash input bits (per-block trigger PC; berti=0 -> learned-off)
  int pp_weight_cap = 64;          // clamp for per-input weights
  int pp_bits() const { return pp_hist_bits + pp_pc_bits; } // total perceptron input width
  // Jimenez-Lin surprise gate: only train a (positive,negative) pair when they are not yet
  // well-separated (pc[pos]-pc[neg] < online_theta_train), so confident predictions aren't
  // over-trained. 0 = gate off (always train).
  int online_theta_train = 0;

  // --- Table sizes ---
  std::size_t pattern_table_sets = 512; // PROMOTED: sized to hold the PC-widened key space (pattern_pc_bits=4)
  std::size_t pattern_table_ways = 2;
  // Separate NEGATIVE (backward) table geometry, decoupled from the forward table. Backward patterns are far
  // fewer than forward, so this can be sized down independently (tolerating conflicts) to reclaim state while
  // preserving mcf's backward coverage. 0 = inherit the forward pattern_table_sets/ways.
  std::size_t negative_table_sets = 0;
  std::size_t negative_table_ways = 0;
  std::size_t pattern_conf_sets = 1;
  std::size_t pattern_conf_ways = 16;
  std::size_t cpt_sets = 128;
  std::size_t cpt_ways = 1;

  // --- Scraping triggers ---
  bool scrape_on_idle = false;   // scrape harvest replaced by online_learning (per-access) by default
  uint64_t scrape_idle_time = 1000;     // cycles of inactivity (uses trace cycle)
  bool scrape_on_count = false;  // (see scrape_on_idle) online_learning is the default harvest path
  uint64_t scrape_min_count = 2;
  uint64_t scrape_access_count = 14;
  bool scrape_on_evict = false;
  bool mark_after_scrape = true;
  bool clear_after_scrape = false;
  bool clear_filter_after_scrape = false; // scrape must NOT wipe the residency shadow (prefetch_map
                                          // doubles as the redundancy filter); wiping it every ~14
                                          // accesses broke redundancy squashing. false = reaffirm resident.
  // Original SPPAM caps the scrape window at the region size (access_map.size()),
  // which truncates/misaligns access patterns in the last ~2*pattern_size blocks
  // of a region and never trains forward-shadow (cross-boundary) patterns. Set
  // true to extend the window over the full shadow map (the alignment fix);
  // false reproduces the original behavior.
  bool scrape_full_window = false;
  bool access_map_miss_only = false;
  bool train_demand_only = false;

  // --- Directionality ---
  bool do_negative = false;  // PROMOTED: gated backward scan captures backward-strided pointer-chase (mcf +47% cov)
  bool separate_negative_tables = true;
  // Backward ONLINE training (learn_on_access): the negative/backward pattern tables are otherwise scrape-only,
  // and scrape is off under online_learning -> do_negative is dead by default. These add backward training online.
  bool neg_online_train = false; // PROMOTED: train backward patterns per-access (else the negative tables stay cold)
  bool neg_train_gated = false;  // PROMOTED: train backward only when the direction is negative (avoids 0-key pollution on forward streams)
  bool neg_dir_pc = false;       // PROMOTED: direction signal = per-IP ip_dir_ (PC-correlated); cleaner than spatial momentum
  // Backward self-throttle: gate the backward scan on its own sampled per-IP usefulness (needs enable_ip_filter).
  // Fires backward until an IP has >= min_samples backward prefetches, then keeps firing only if useful% >= thresh.
  bool bwd_useful_gate = false;       // PROMOTED: self-throttle backward on its own sampled per-IP usefulness (kills forward-stream residue)
  int  bwd_useful_thresh = 50;       // require this useful% to keep firing backward for an IP
  int  bwd_useful_min_samples = 8;   // warmup: fire backward freely until this many samples
  bool scan_forward = true;
  uint64_t scan_distance_forward = 1; // PROMOTED default: single-path (efficiency win vs scan-16; ~same coverage, far fewer wasted prefetches)
  bool scan_backward = true;
  uint64_t scan_distance_backward = 16;
  // --- Delta-PHT (spatial-context -> next-access DELTA) ---
  // The position-bitmap predicts fixed offsets AHEAD/BEHIND the trigger; it structurally cannot emit a next
  // access that lands inside/near the recent window (within |d|<=ps, immediate neighbor). This table keys the
  // access-map neighborhood centered on the PREVIOUS access and predicts the signed delta to the just-accessed
  // line -- direction-agnostic, so within-window / backward / near-neighbor targets become reachable. The
  // signature is inclusive of the access point (always non-zero), so it never trains the illegal 0-key.
  bool delta_pht = false;             // master enable (supplements the position bitmap)
  int delta_sig_radius = 6;           // centered access-map window radius -> (2r+1)-bit signature
  int delta_pht_sets = 512;
  int delta_pht_ways = 4;
  int delta_pht_conf_max = 15;        // saturating confidence ceiling
  int delta_pht_conf_min = 2;         // min confidence to emit a prediction
  int delta_pht_degree = 2;           // per-trigger prefetches (re-lookup at each predicted position)
  int delta_pht_max = 32;             // clamp |delta| trained/predicted (page-bounded anyway)
  // SPP-style path confidence: accumulate product of (conf/conf_max) down the lookahead; once it drops below
  // this floor, stop extending (the current hop still issues). 0 => no decay gate (depth bounded only by degree).
  double delta_pht_path_conf = 0.0;
  // Usefulness SELF-THROTTLE: track the delta-PHT's own useful/useless rate (EMA) and cut speculative DEPTH where
  // it is inaccurate (irregular graph traces), keeping a shallow probe alive to re-sample. Mirrors the depth-throttle.
  bool delta_pht_selfthrottle = false;
  double delta_pht_min_acc = 0.55;    // additive-fraction EMA below which delta MAY be suppressed (AND-ed with unused)
  double delta_pht_max_unused = 0.15; // unused-evict (pollution) EMA above which delta MAY be suppressed (diagnostic)
  double delta_pht_max_hitrate = 0.90; // demand hit-rate above which delta is suppressed (saturated -> no headroom)
  // SET-DUELING throttle: the harm (cross-prefetcher displacement) is invisible per-block, so measure delta's NET
  // effect directly -- reserve OFF-leader L2 sets (never delta) vs ON-leader sets (always delta), a PSEL counter
  // tracks which misses less, and follower sets follow the winner. This is the only signal that sees displacement.
  bool delta_pht_setduel = false;
  int delta_pht_sd_period = 64;       // 1 OFF-leader + 1 ON-leader per this many L2 sets
  int delta_pht_sd_max = 1024;        // PSEL saturation (followers use delta iff 2*PSEL >= max)
  int delta_pht_warmup = 2000;        // outcomes observed before the throttle engages (learn the EMA first)
  int delta_pht_explore = 64;         // when suppressed, still fire a 1-deep probe every Nth trigger (keep sampling)
  int delta_pht_ema_shift = 8;        // EMA alpha = 1/(1<<shift)
  // Cold-start table (prototype): on a NEW region entry the access-map pattern is unlearned, so the +1/+2
  // page-entry-startup strides are missed. A tiny per-trigger-PC local delta predictor kicks a starting
  // prefetch stream at region creation. cold_start_pc=false => fixed forward next-N; true => learned dominant
  // delta per trigger-PC (falls back to +1 when cold). Gated OFF by default (faithful shipping unchanged).
  bool enable_cold_start = false;
  uint64_t cold_start_degree = 3;    // prefetches issued at a fresh region entry
  bool cold_start_pc = false;        // key a learned dominant delta by trigger-PC hash (else fixed +1..+N)
  uint32_t cold_start_entries = 256; // PC-keyed delta table size (power of 2)
  // Cross-page PHT (prototype): a SEPARATE table trained ONLY on region crossings. Key = the PREDECESSOR
  // region's spatial map (the signature it fed the main PHT) + entry offset; value = the NEW region's access
  // pattern. Predicts a fresh region's footprint from the page we came from -- the cross-page structure the
  // per-region PHT is blind to at entry. Shallow (no deep lookahead). Gated OFF by default.
  bool enable_cross_page = false;
  uint32_t cross_page_degree = 12;   // max prefetches issued per region entry from the cross-page table
  double cross_page_conf = 0.33;     // cnt/occ threshold to prefetch a predicted offset
  uint32_t cross_page_min_occ = 2;   // min occurrences of a key before it predicts
  int cross_page_key = 0;            // key source: 0=SPATIAL (predecessor map signature), 1=IP (crossing PC), 2=both
  // Region-table eviction POLICY (attacks the region-EVICTED temporal residual). PURE policies (no LRU blend):
  //  0 = LRU (default)  1 = MRU (evict most-recently-used, keep old)  2 = RANDOM
  //  3 = ENTROPY (keep access-maps closest to 50/50 = predictable-but-not-done; evict full/empty extremes)
  //  4 = evict HIGH RESIDENCY (prefetch_map most FULL = fully cached / done)
  //  5 = evict LOW RESIDENCY  (prefetch_map most EMPTY = L2 footprint already gone)
  //  6 = evict by USEFULNESS  (region whose prefetches were useless: pf_dead-pf_used high; age tie-break)
  //  7 = evict FEWEST MISSES  (region with the fewest demand misses = least prefetch value; age tie-break)
  int region_evict_policy = 0;
  // Two-level region table: new regions OBSERVE in a small staging table (they still predict); on staging eviction
  // only GOOD-SHAPE regions (not severely fragmented / not fully-used = medium activity) are PROMOTED to the large
  // table. Filters thrash at PLACEMENT rather than eviction -> keeps the big table's context without re-prefetch.
  bool enable_two_level = false;
  std::size_t stage_sets = 64;
  std::size_t stage_ways = 4;
  int stage_promote_min = 3;         // min touched blocks to promote (below = severely fragmented -> drop)
  int stage_promote_max = 29;        // max touched blocks to promote (above = fully used/done -> drop)
  int forward_momentum_min = -9;   // forward prefetch allowed when momentum > this
  int backward_momentum_min = 8;   // backward prefetch allowed when momentum < this
  // Direction by IP instead of the noisy per-region momentum. Momentum is few-sample, thrashed, and its
  // default thresholds make its own gate a no-op (both directions always scan -> wrong-way pollution). A
  // per-IP saturating direction counter (by ip_hash) learns each trigger-PC's dominant stride direction;
  // a confident IP prefetches ONLY that direction. Fixes predictable-but-large-stride pages (cactuBSSN).
  bool ip_direction = false;
  int ip_direction_min = 4;        // |per-IP direction counter| >= this -> commit to that one direction
  // berti forwards the SOURCE PC (and direction) in pf_metadata (bits 9-31, mask 0x7fffff). Without this,
  // berti's prefetches reach L2 with ip=0 -> ALL collapse into ip_hash bucket 0 (one aggregate mass), so the
  // per-IP filter/direction/perceptron are blind on the berti-dominated stream. Decode + use it as the
  // effective trigger IP so those mechanisms see the real source. No cache change needed.
  bool use_berti_src_ip = false;
  // POLLUTION FILTER (from sppam_b): on a USELESS prefetch eviction, KEEP its prefetch-map bit set so the
  // block reads as resident and is NOT re-prefetched (don't re-waste bandwidth on a proven-useless block).
  // Only USEFUL/demand evictions clear the bit (re-prefetch allowed). Reset naturally on region eviction.
  bool pollution_filter = false;        // |per-IP direction counter| >= this -> commit to that one direction
  bool cross_page = false;

  // Diagnostic: at region eviction, accumulate the access-map fill distribution + effective-storage-granularity
  // (how coarse a bitmap could represent each footprint, and the over-fetch cost of coarsening). For the
  // region-map compression study (DSpatch-style). No effect on prefetching; dumps to stderr at teardown.
  bool region_density_report = false;

  // --- Aggression ---
  // NOTE: the no_lookahead win was measured under the INVALID demand-only model (berti
  // prefetches dropped -> wrong access stream + uncounted berti-serving benefit).
  // Reverted to true pending re-validation under the physical berti-aware model.
  bool do_lookahead = true;
  uint64_t lookahead_depth = 100;
  uint64_t lookahead_conf_cutoff = 7;
  uint64_t lookahead_conf_factor = 13;
  // --- Walk-accumulate prediction (alternative to end-over-end lookahead) ---
  // Instead of prefetching a whole predicted pattern and chaining it, WALK one nearest-predicted offset at a
  // time (re-forming the spatial signature from map+speculative-path at each step). The nearest bit is the
  // immediate step; other set bits ACCUMULATE per-absolute-offset evidence (prediction confidence). An offset
  // is prefetched only when its accumulated evidence >= a threshold that GROWS with distance from the trigger,
  // so deep prefetches need corroboration from multiple overlapping lookups instead of one pattern's say-so.
  bool walk_accumulate = false;        // master enable (replaces the forward end-over-end lookahead)
  int walk_max_depth = 16;             // max walk steps
  int walk_thresh_base = 1;            // corroborations (predicting walk-steps) needed at distance 1
  int walk_thresh_slope = 5;           // extra corroborations required per block of distance (/10; fractional)
  int walk_thresh_cap = 3;             // CAP on required corroborations (a far offset can't need more than the walk can give)
  int walk_jump_relief = 1;            // reduce the requirement by this per block of REACH>1 (big jump = fewer chances)
  int walk_advance = 0;                // 0 = advance to NEAREST predicted; 1 = advance to MOST-CORROBORATED (conf,dist tiebreak)
  int walk_degree = 8;                 // per-trigger prefetch cap (generous backstop; the threshold should govern)
  bool walk_usefulness_throttle = false; // scale the walk's effective degree by global usefulness (like e2e's current_pf_degree_):
                                         // unreliable workload -> shallower path (conservative role-2 behaviour)
  bool walk_replace = false;             // false = walk COEXISTS with the e2e forward path (role-2 alongside role-1);
                                         // true = walk REPLACES the forward e2e path (A/B measurement of the walk alone)
  bool walk_fallthrough = false;         // SELECTOR (proper SPP fall-through): run the walk ONLY where role-1 is weak at
                                         // the trigger -- silent (no learned pattern) OR its pattern usefulness < walk_ft_usefulness
  int walk_ft_usefulness = 8;            // e2e trigger-pattern usefulness (0-15) below which role-2 (walk) takes over
  bool walk_setduel = false;             // SELECTOR via SET-DUELING: reserve walk-OFF vs walk-ON leader L2 sets, a PSEL
                                         // tracks which misses less, follower sets follow the winner (reuses delta_pht_sd_period/max)
  // PRECISE PATTERN VALIDATION for proper fall-through. modify_pattern_usefulness SPRAYS credit over every pattern
  // that COULD have predicted a resolved block; instead SAMPLE the ACTUAL triggering pattern at issue (block->pat_key)
  // and, when that prefetch resolves useful/useless, credit ONLY that pattern. A pattern with low validated accuracy
  // is BAD -> SUPPRESS e2e there and FALL THROUGH to the walk (role-2). Fixes "e2e always has a weak prediction so
  // fall-through never fires" -- now it fires exactly on the patterns the sampling proves are wrong.
  bool pattern_validate = false;         // enable precise per-pattern validation + bad-pattern fall-through to the walk
  int pv_sample_div = 4;                 // sample 1/N issued e2e prefetches into the block->trigger-pattern table
  int pv_min_samples = 8;                // resolved samples for a pattern before it can be judged bad
  int pv_bad_pct = 40;                   // validated accuracy (%) below which a pattern is BAD (fall through)
  std::size_t pv_sample_cap = 8192;      // block->pattern sample table capacity (bounded)
  // pf_sample_ replacement policy. false = the legacy unbounded map with clear-on-full (wipes ALL in-flight
  // samples periodically -> a small table churns and loses most eviction resolutions). true = a FIXED
  // direct-mapped table with PROBABILISTIC eviction: an in-flight incumbent survives ~pv_sample_evict_div
  // collisions before a new sample can displace it, and is reclaimed once older than pv_sample_ttl ops. This
  // keeps the residency time of a sample matched to the prefetch lifetime, so a much smaller table validates
  // as well (the sampling rate is no longer out of sync with the table size).
  bool pv_sample_directmap = false;
  uint32_t pv_sample_ttl = 4096;         // ops before an unresolved in-flight sample becomes reclaimable
  uint32_t pv_sample_evict_div = 4;      // on a collision, replace the incumbent only 1/N of the time
  // Adaptive sampling rate (directmap only). The sample table is a short-lived issue->resolve holding area;
  // with only ~64 active IPs and PERSISTENT decaying per-IP/pattern counters, we don't need many CONCURRENT
  // samples -- so we can shrink the table hard and SAMPLE LESS to match. This controller self-tunes
  // ip_sample_div to hold the rate at which live (unresolved) incumbents are displaced -- the "forced
  // eviction" churn -- inside [pv_churn_lo, pv_churn_hi]%: too much churn -> sample less (raise div), plenty
  // of headroom -> sample more (lower div). Decouples table size from prefetch volume.
  bool pv_adaptive_rate = false;
  uint32_t pv_rate_window = 2048;        // sample placements per rate adjustment
  uint32_t pv_churn_hi = 25;             // % of placements displacing a live incumbent, above which -> sample less
  uint32_t pv_churn_lo = 6;              // ...below which -> sample more
  uint32_t pv_div_min = 4;               // sample-rate divisor floor
  uint32_t pv_div_max = 256;             // ...and ceiling

  // --- Perceptron prefetch filter (PROTOTYPE; predictor logic lives in the DSE copy until validated) ---
  // One LEARNED gate that supersedes the heuristic throttles (ip-filter volume/depth, backward gate, etc.).
  // Hashed-perceptron: score = sum of small per-feature weight tables; keep if score >= perc_tau_keep. Features:
  // trigger PC, region offset, engine, lookahead depth, + the pre-aggregated per-IP usefulness/timeliness buckets.
  // Trained at sample resolution on usefulness (perc_label_pe -> PE-sign instead). See perceptron_filter_design.md.
  bool enable_perceptron_filter = false;
  std::size_t perc_pc_entries = 1024;    // per-PC weight table depth (power of two)
  int perc_weight_max = 31;              // saturating weight bound (~6-bit signed)
  int perc_tau_keep = 0;                 // keep if score >= this (<=0 = permissive cold start)
  int perc_theta_train = 24;             // train when |score| < this OR the decision was wrong (confidence margin)
  uint32_t perc_explore_div = 16;        // issue 1/N of "drop" decisions anyway (exploration -> counterfactual labels)
  bool perc_label_pe = false;            // train on PE-sign (needs the glue's PE sampling) instead of usefulness
  int perc_pe_margin = 0;                // with perc_label_pe: only train when |PE| exceeds this (skip the noisy middle)
  double perc_pe_scale = 30.0;           // PE magnitude -> training-step size (~L_LLC): high-PE (DRAM-covering) hits train harder
  int perc_pe_step_max = 4;              // cap the per-example weight step so no single high-PE outcome dominates
  // enabled feature tables. bits: 0x1 PC, 0x4 eng, 0x8 depth, 0x10 use, 0x20 tim, 0x40 conf, 0x80 sig, 0x100 mshr,
  // 0x200 guse, 0x400 gtim, 0x800 hit, 0x1000 conj, 0x2000 pip-bias, 0x4000 pcpath, 0x8000 pcdelta, 0x10000 use^mshr,
  // 0x20000 tim^mshr, 0x40000 conf^mshr, 0x80000 criticality. Default = DSE-derived strong set (contention-XORs +
  // criticality + PC/sig/path); drops the weak/superseded ones (guse, gtim, coarse conj, pip-bias, offset).
  uint32_t perc_feat_mask = 0xFC9F5;
  bool perc_pe_signonly = false;         // variance-reduce PE: train on sign(PE)+margin with a FIXED step (not magnitude-scaled)
  std::size_t perc_sig_entries = 1024;   // per-prediction spatial-pattern-signature table (SPPAM key); orthogonal to per-PC usefulness
  bool perc_pe_norm = false;             // variance-reduce PE: normalize by a ROLLING MEAN of |PE| before quantizing (scale-invariant -> robust to the DSE's unreliable absolute latencies); step then = |PE|/mean clamped to perc_pe_step_max
  bool perc_pe_cost = true;              // fold the prefetch's COST (I_LAT bandwidth at fill + I_POLL pollution on victim re-miss) into its per-prefetch perceptron PE. Without it the reward is I_UPF-only (useless~=-L_LLC) and the DSE showed that gives almost no filtering signal -- one DRAM-covering hit outweighs ~54 useless fills. The COST terms are where the contention features get their signal.
  bool perc_untimely_veto = true;        // DSE lesson [[dse-lessons-consolidated]]: NEVER volume-drop an UNTIMELY prefetch (right address, evicted-before-use) -- that kills coverage on pointer-chasers (mcf). depth-throttle handles untimely by SHORTENING lookahead. So the perceptron may only drop TRULY-BAD (wrong-address) IPs: when the trigger IP's untimely fraction (ip_untimely_/ip_ev_) >= ip_untimely_thresh, VETO the drop (force keep, leave it to depth-throttle). Complements ip_filter_depth_throttle, does not replace it.
  bool perc_load_gate = false;           // CONTENTION-GATE the drop (DSE lesson #3, the datacenter residual): PE mis-aligns with IPC on bandwidth-bound datacenter/graph workloads, so the PE-trained perceptron drops GENUINELY-USEFUL prefetches there (merced/sierra/tahoe ~-1..-2%). Those workloads run at LOW MSHR occupancy, so only DROP when the trigger's MSHR/bandwidth pressure index >= perc_load_gate_thresh -- under light load a dropped prefetch cannot be hurting anyone, so keep it and spare the datacenter. High-pressure single-core (xalan) keeps dropping. Complements untimely-veto (which routes by WHY: untimely->depth); this routes by WHEN (only drop under real contention).
  int perc_load_gate_thresh = 2;         // MSHR-pressure index (0..15) below which perc_load_gate force-keeps a would-be drop. (TESTED INEFFECTIVE: datacenter is bandwidth-bound = HIGH mshr, so the gate never fires -- kept off. Premise was backwards.)
  bool perc_instr_gate = false;          // INSTRUCTION-ACTIVITY GATE (the datacenter fix): the DATA perceptron only helps on data-bound workloads (mcf/xalan); on instruction-bound datacenter/graph workloads (merced/tahoe/sierra) dropping data prefetches only loses coverage (measured: perceptron NEVER beats pe-management-alone there, even at max sampling). The clean runtime discriminator is instruction-prefetch activity: instr_pf/(instr_pf+data_pf) is ~0.00 on mcf, 0.15 on xalan, but 0.5-0.6 on the datacenter. When that fraction >= perc_instr_gate_pct, the perceptron backs off (force-keep) so the datacenter -> ~pe-management; mcf/xalan (fraction ~0) stay fully aggressive.
  int perc_instr_gate_pct = 40;          // instruction-pf fraction (%%, of instr+data prefetch issues) at/above which perc_instr_gate force-keeps a would-be drop. 40 cleanly separates xalan(~15) from the datacenter(~53-60).
  // PC feature ENCODING (PCs are few -> a full 48-bit hash injects entropy/noise; a targeted slice keeps the signal).
  int perc_pc_encoding = 1;              // 0 = multiply-hash (legacy), 1 = contiguous bit SLICE [lobit .. lobit+log2(entries)), 2 = random DROP-OUT (fixed wide-ranging non-consecutive bits)
  int perc_pc_lobit = 2;                 // slice: low bit (drop the bottom lobit alignment-noise bits)
  uint64_t perc_pc_dropmask = 0x5555555555555555ull; // drop-out: which PC bits to keep (pext-style), then pack low
  bool perc_dump_weights = false;        // at teardown, print accumulated weight magnitude per feature table + top entries (which features carry signal)
  bool perc_gate_instr = false;          // gate branch-graph (instruction) prefetches through the perceptron too; false = data engines only (BG uses its own instr_conf gate). Datacenter prefetching is instruction-DOMINATED and the data-tuned features can't discriminate BG prefetches (BG is ~83% net-positive / nearly UNFILTERABLE, see [[perceptron-strong-features]]). Measured: gating BG costs -3..-5% on merced/sierra/tahoe; un-gated ~-1%. So default FALSE.
  bool perc_engine_split = false;        // PER-ENGINE feature weights for the context features (use/tim/conf/dep/mshr/guse/gtim/hit): each table is indexed by (engine, bucket), so the SAME feature value scores differently for a branch-graph vs a data prefetch. The discriminator between "gate all engines with one shared model" (current) and "every feature 4-way per-engine" (extreme).
  bool perc_profile = false;             // FEATURE-SIGNAL PROFILER (analysis, not gating): accumulate per-feature-value -> useful/useless histograms at every resolve, then at teardown report the mutual information I(feature;outcome) GLOBALLY and CONDITIONED ON ENGINE. Finds which features carry strong signal and which need engine-partitioning BEFORE tuning. Keep perc_tau_keep very low so nothing is dropped (profile the full stream).
  std::size_t perc_track_cap = 256;       // direct-mapped feature-snapshot table depth (po2); a kept prefetch's feature
                                         // vector is held until its hit/eviction outcome trains the perceptron -- the
                                         // dense training trigger (sampled per-IP usefulness/timeliness are FEATURES,
                                         // not the trigger). Small: the eviction policy tracks the prefetch lifetime.
  // DENSE useful/useless training (the DSE architecture): the DSE ranks features by AGGREGATE (coverage-weighted) PE,
  // but sparse per-prefetch PE training (1/pe_sample_div, step ~ |PE|) over-weights mcf's DRAM-covering hits and only
  // learns mcf. Usefulness (+1 used / -1 evicted) needs NO latency, so it can train on EVERY prefetch -- and per-event
  // ±1 naturally coverage-weights (a 100-miss pattern gets 100 events), matching the DSE's aggregate view. Features are
  // still the DSE strong set (perc_feat_mask, unchanged). Store the gate feature vector per issued prefetch keyed by
  // block; train at its hit (useful) or unused-eviction (useless). See [[perceptron-instr-gate]] [[perceptron-strong-features]].
  bool perc_dense_train = false;          // ON = dense useful/useless training (block-keyed snapshot, every prefetch); OFF = legacy sparse-PE (perc_track_ 1/pe_sample_div). Forces the ±1 usefulness label.
  std::size_t perc_dense_cap = 16384;     // dense snapshot table depth (po2): must hold in-flight (issued-but-unresolved) prefetches without heavy churn. Overwrite-on-collision (each prefetch resolves once).
  // VICTIM TABLE (PPF-style anti-over-filter): sample DROPPED prefetches; if a dropped block is later DEMANDED, the drop
  // was BAD (lost coverage) -> train that feature vector back toward KEEP with a HEAVY step. This is what stops the
  // dense filter from over-dropping useful prefetches (the whole failure mode). See the user's design + PPF reject table.
  bool perc_victim = false;               // enable the victim table (only meaningful with perc_dense_train).
  std::size_t perc_victim_cap = 8192;     // victim table depth (po2), block-keyed, overwrite-on-collision.
  int perc_victim_sample_div = 1;         // sample 1/N of real drops into the victim table (1 = every drop).
  int perc_victim_penalty = 4;            // heavy KEEP training step applied when a dropped (victim) block is re-demanded.
  // PE GATE (per-IP sampled PE decides whether filtering can even help): high per-IP PE => prefetching helps regardless of
  // usefulness => VETO the drop; low/bad PE + high useless => filter. PE is the "does dropping help performance" signal,
  // separate from the usefulness the perceptron predicts. Per-IP avg PE is sampled from prefetch fills (perc_note_ip_pe).
  int perc_pe_ema_shift = 4;              // per-IP PE components are a ROLLING EMA (alpha = 1/2^shift), NOT a run-cumulative avg -- so PE tracks RECENT phase behavior, not a whole-run number that drowns the signal. 4 => alpha 1/16.
  // Per-COMPONENT quantization scale (cycles per feature bucket): the 3 PE terms have very different magnitudes
  // (I_UPF ~200, I_LAT ~10, I_POLL ~500), so a shared scale kills I_LAT (->bucket 0) and saturates I_POLL. Each /scale, clamp 0..15.
  int perc_upf_scale = 32;                // I_UPF ~0..480 cyc -> buckets 0..15
  int perc_lat_scale = 1;                 // I_LAT ~0..12 cyc (the Eq4 service cost) -> buckets 0..12
  int perc_poll_scale = 48;               // I_POLL ~0..720 cyc -> buckets 0..15
  // PER-EVALUATION LOGGING (diagnostic): sample 1/perc_eval_log_div DROPS and print each ACTIVE feature's CONTRIBUTION
  // (weight-table value) + bucket to the final score -> post-process which features drive GOOD vs BAD drops. A drop is
  // BAD iff its block is later demanded (a victim-table hit) -> logged as PBAD; join PDROP<->PBAD by block. No averages.
  bool perc_eval_log = false;
  int perc_eval_log_div = 512;
  bool perc_sig_xor_pc = false;           // mix the trigger PC into the signature index (in addition to engine) -> disambiguates (PC,sig) pairs that currently ALIAS to one weight and cause misattribution (the tahoe bad-drop driver).
  bool perc_pe_gate = false;              // enable the per-IP PE veto on drops (only meaningful with perc_dense_train).
  int perc_pe_gate_thresh = 60;           // veto a drop when the trigger IP's sampled avg PE >= this (units = L_LLC-ish; DRAM-covering ~ hundreds). ~60 clears the mostly-LLC IPs, spares DRAM-covering-but-low-usefulness ones.
  int perc_pe_gate_min_samples = 8;       // require this many sampled PE observations for the IP before the gate trusts it.
  uint32_t perc_track_ttl = 65536;       // ops before a pinned perc_track_ sample times out USELESS. DEDICATED (not the
                                         // shared pv_sample_ttl): perc_track_'s clock is the predictor OP counter, and a
                                         // SHORT ttl times out useful-but-not-yet-demanded prefetches as useless-on-timeout
                                         // -> useless-heavy training -> 90% drop -> mcf -30%. Long ttl lets coverage
                                         // prefetches (pointer-chasers) reach their demand hit before timing out (mcf +2%).
  // Feed the precise validation back INTO the confidence values (prediction_counter): a proven-useless prediction
  // is penalized so it falls below threshold and the position-bitmap goes SILENT there (natural fall-through to SPP);
  // a proven-useful one is reinforced. This is validation-as-confidence-contributor (vs pv_bad_pct's separate gate).
  bool pv_feed_confidence = false;
  int pv_conf_penalty = 8;               // prediction_counter decrement on a proven-useless sampled prediction
  bool prob_drop_prefetches = false;  // OFF permanently: its current_usefulness-crush over-drops (esp. with a small region table); see analysis
  bool global_or_pattern_usefulness = true; // true = pattern usefulness, false = global
  bool adaptive_usefulness = true;
  uint64_t pattern_usefulness_cutoff = 7;
  bool use_default_prediction = true;
  uint64_t default_pattern = 0b111;
  // default_prediction defaults to 1 << (pattern_size-1); 0 means "derive".
  uint64_t default_prediction = 0;
  bool prefetch_demand_only = false;
  // usefulness[0..15] -> prefetch degree; drop chance out of 128.
  std::array<int64_t, 16> prefetch_degrees_usefulness = {1, 1, 2, 2, 2, 3, 3, 3, 4, 4, 8, 8, 12, 12, 16, 16};
  std::array<int64_t, 16> prefetch_drop_chance_usefulness = {123, 120, 110, 110, 80, 50, 10, 0, 0, 0, 0, 0, 0, 0, 0, 0};
  // --- Bandwidth feedback (reinstated from the original SPPAM; needs timing) ---
  // bw_util index = f(DRAM utilization x LLC miss rate); throttles prefetch degree
  // by prefetch_degrees_bw[bw_util]. bw_mult: multiply usefulness by the BW degree
  // (original BW_MULT); else subtract it from the usefulness degree. Also scales
  // SPP's effective lookahead down as bandwidth rises.
  bool enable_bw_feedback = true; // reinstated SPPAM bandwidth feedback (subtract mode)
  bool bw_mult = false;
  // Bandwidth market: a shared, floating admission threshold on per-prefetch
  // benefit (expected usefulness ~ coverage/bandwidth, in [0,1], comparable across
  // SPPAM/SPP and across cores). Under DRAM pressure the threshold rises so only
  // the highest-benefit prefetches SYSTEM-WIDE are admitted -- a RELATIVE
  // cross-core prioritization, not an absolute per-core cutoff.
  // Per-prefetcher PE management (I-POP): each phase, compute PE = I_UPF - I_LAT
  // per prefetcher (pollution term dropped -- measured negligible at L2); disable
  // (ignite-only-positive) any with PE <= 0 for pe_off_phases phases, then retest.
  // PE = i_upf - i_lat directly targets AVG LATENCY (latency saved minus latency
  // added by delaying demands) -- THE optimization objective. By LATENCY it is a
  // clear net win: big gains exactly where prefetch hurts latency (xalan 1.76->1.51,
  // mcf 1.10->1.03, cc5 1.20->1.15) at negligible cost on winners (charlie +0.009;
  // sierra/lbm improve). [An earlier miss-rate-based read wrongly called it harmful
  // -- miss rate DISAGREES with latency; latency is the target.] Best paired with
  // bw_feedback (pe_fb). See [[sppam-dse-objective]], [[sppam-dse-findings]].
  bool enable_pe_management = true;
  uint64_t pe_phase = 1024;       // demands per PE evaluation phase
  uint64_t pe_throttle_div = 8;   // non-positive-PE prefetcher throttled to 1/N (NEVER gated -- a
                                  // complete gate is unrecoverable: no prefetch -> no PE signal)
  std::size_t pfht_entries = 256;  // shared sampled prefetch-outcome table (PE + per-IP filter attribution)
  uint32_t pfht_tag_bits = 8;     // hashed PfHT tag width (for the state estimate)
  uint64_t pe_sample_div = 16;    // shared attribution: track 1/N issued prefetches (probabilistic sampling,
  // RULE: pe_sample_div MUST scale with 1/pfht_entries. A small table needs a SPARSE sample or it saturates ->
  // biased per-IP/PE attribution -> mis-throttle (pfht_ 256 @ div 2 tanked mcf -30%; 256 wants div ~16, matching
  // the proven 2048 @ div 2 occupancy). Same table-size<->sample-rate coupling as perc_track_. Keep them matched.
                                  // same anti-thrash trick as spp_pf_sample_div -- few live PfHT entries
                                  // so each survives long enough to resolve). PE sign is scale-invariant.
  double pe_pf_demand_weight = 0.5; // Berti's prefetch-stream accesses count as DEMAND in the PE terms
                                  // (berti is usually useful / self-throttling) but at this weight (<1):
                                  // a delayed/served prefetch has more slack than a true load. Weights
                                  // the NUMERATOR (latency saved/polluted/induced); bandwidth counts stay 1.
  // I_LAT = N_Delay*T_NoC + N_Bus*T_Bus + N_Bank*T_Bank (I-POP Eq4): the delay a prefetch
  // imposes is the SERVICE TIME it occupies a shared resource (NOT queueing -- stable, so it
  // survives multi-core). Charged once per OUR-prefetch fill that finds a demand in the MSHR
  // (MAX-induced-delay rule). Two lumped service times in L2 cycles, grounded in this config:
  //   DRAM-served pf:  T_Bus(64B/buswidth burst ~7) + amortized T_Bank(tCAS+RBmiss*(tRP+tRCD) ~60-180
  //                    over ~banks) + T_NoC.  LLC-served pf: only the on-chip path (T_NoC).
  double pe_serv_dram = 12.0;     // shared-resource service time a DRAM-served prefetch imposes (cycles)
  double pe_serv_llc = 4.0;       // ...an LLC-served prefetch imposes (on-chip interconnect only)
  double pe_dram_lat_threshold = 64.0; // fill latency above which a prefetch was DRAM- (not LLC-) served
  // RETIRED (off by default): the demand-clearing market priced a core's prefetches
  // against OTHER cores' DEMAND, so it cratered victims that NEED their prefetches
  // (cc-5 mixes 1.18->0.97) and only helped when the victim's prefetches were
  // genuinely low-value (xalan). Net-harmful on harmonic mean. Replaced by the
  // rank-order bandwidth throttle below.
  bool enable_bw_market = false;       // demand-clearing prefetch admission (retired)
  double bw_market_target_util = 0.70; // DRAM utilization the market steers toward
  double bw_market_step = 0.005;       // threshold adjustment per DRAM access
  uint64_t market_trickle_div = 8;     // even a below-threshold prefetch issues 1/N (never fully
                                       // gated -- keeps the usefulness/PE feedback alive)
  std::array<int64_t, 16> prefetch_degrees_bw = {0, 0, 1, 1, 1, 1, 2, 2, 4, 4, 8, 8, 12, 12, 16, 16};
  // --- Region-map thrash throttle (universal "can't learn" signal) ---
  // When the region map thrashes -- regions evicted having touched only a block or
  // two, so no pattern/signature ever accumulates -- NEITHER SPPAM nor SPP can learn,
  // and both should back off (e.g. xalancbmk under a berti L1: best to switch off).
  // Signal: EMA of the fraction of evicted regions that "died young" (<= min_blocks
  // distinct blocks). Distinguishes thrash from STREAMING (streaming regions die with
  // many sequential blocks set -> not young). Mapped to a shared [0,1] throttle that
  // drops a fraction of BOTH engines' prefetches, with a floor trickle to recover.
  // DEFAULT OFF: built + measured, but region-map reuse does NOT isolate the
  // workloads where prefetch hurts LATENCY (xalan is cold-streaming -> re-reference
  // 0.085, yet prefetch is catastrophic by latency 1.76; cc5 re-reference 0.80 also
  // a loser; sierra re-reference 0.64 is a WINNER). The real "should I throttle"
  // signal is latency cost vs benefit (i_lat vs i_upf), not region thrash. Kept for
  // ablation. See [[sppam-dse-findings]].
  // Region staging GATE: keep sparse graph-walk pages out of the region table so they don't
  // capacity-evict the dense set (drop-gate: DSE showed +25% coverage vs a same-size table on graph/DC).
  // Two predictors of "this page will be sparse", separately toggleable and compared:
  //  (1) SPATIAL: a page must accumulate >= staging_promote_threshold demand hits in a small
  //      direct-mapped staging filter before it earns a region entry (measures density directly).
  //  (2) IP: a per-IP table (indexed by ip_hash, 256 buckets) learns whether pages a given trigger-IP
  //      allocates end up dense or sparse (density = access_map popcount at eviction); gate new pages
  //      from IPs whose pages evict sparse. Cheaper/faster than waiting to measure each page.
  // Optionally ADAPTIVE: only gate when region_thrash() is high, so streaming (no thrash) is pass-through.
  bool enable_region_staging = false;      // spatial gate
  std::size_t staging_entries = 2048;
  uint32_t staging_promote_threshold = 4;
  bool enable_ip_gate = false;             // IP-confidence gate: REUSES the ip-filter's per-IP table (no new state).
  uint32_t ip_gate_div_min = 2;            // gate allocation when ip_trickle_div(ip) >= this (IP is being throttled =
                                           // low prefetch yield = sparse pages). Needs enable_ip_filter (default on).
  uint32_t ip_gate_explore_div = 8;        // every Nth GATED allocation, admit anyway so the IP still generates
                                           // prefetches -> stays sampled -> can recover. NEVER permanently gate an IP.
  bool gate_adaptive = false;              // apply gate(s) only when region_thrash() >= gate_thrash_min (needs thrash throttle on)
  double   gate_thrash_min = 0.55;         // region_thrash_ema threshold above which the gate engages

  // --- Branch-graph L2 instruction prefetcher (see iprefetch_predictor.h) ---
  // Models the L1I-filtered instruction miss stream the L2 sees. OFF by default: the
  // data path is unchanged and instruction fetches stay blocked (DPC4 parity). When on,
  // the module opts the cache into delivering instruction accesses and routes them to a
  // separate branch-edge predictor (its own residency filter; never touches the region
  // table). Physical-address space (L2 is physically indexed) -> issue needs no translation.
  bool enable_instr_prefetch = true; // ON by default: +5.8% datacenter / +0.08% control, no regression,
                                     // ~2.5 KiB. Instruction prefetching is now part of SPPAM. (DSE-validated
                                     // IP-space branch graph; set false to get the data-only prefetcher.)
  // Marginal-value experiment knobs: instr_feed_data lets instruction fetches ALSO train the data
  // path (no early return after the branch graph) so we can measure the branch graph's contribution
  // ON TOP of the data path handling instructions. unblock_instructions opts the cache into
  // delivering instruction fetches even with the branch graph OFF (the data-path-only arm).
  bool instr_feed_data = false;
  bool unblock_instructions = false;
  // v2 instruction refinements (DSE holistic-id/bg_residency findings; default OFF = B, C enables):
  int instr_nextn = 0;                 // sequential fallback: prefetch this many phys blocks ahead within the code page (next-2 ~= full SPPAM on the sequential residual)
  bool instr_packed_residency = false; // branch graph shares SPPAM's PACKED code residency (4KiB-page/both-maps) instead of its own filter -> lower redundancy, zero extra state
  int instr_la_depth = 8;            // lookahead ceiling (max blocks walked ahead per access)
  int instr_conf = 60;              // confidence gate (percent): walk deeper only while dominant-edge share >= this
  // Realistic IP-space encoding (validated in the DSE): cov/acc are flat down to 256 edges, so the
  // table is tiny. Edge = 16-bit hashed tag + valid + 2x 8-bit IP-delta + 2x 4-bit count = 41 bits;
  // the residency filter holds a 16-bit block tag (approximate), not a full address. ~2.6 KiB total.
  std::size_t instr_table_entries = 256;   // branch-edge table slots (rounded up to a power of two)
  int instr_delta_bits = 8;               // signed IP-block delta width (successor = ip + delta)
  std::size_t instr_xlate_entries = 32;    // vpage->ppage translation slots (rounded up to a power of two)
  std::size_t instr_filter_entries = 512;  // physical residency-filter buckets (rounded up to a power of two)

  bool enable_region_thrash_throttle = false;
  uint32_t region_thrash_min_blocks = 2;  // (unused by re-reference detector; kept for ablation)
  std::size_t region_thrash_table = 4096; // recently-evicted region tag buffer (re-reference detector)
  double region_thrash_lo = 0.55;         // young-evict fraction below which there's no throttle
  double region_thrash_hi = 0.85;         // ...and at/above which the throttle is at full strength
  double region_thrash_max_drop = 0.95;   // max prefetch-drop fraction at full thrash (<1 -> trickle)

  // --- Rank-order bandwidth throttle (shortest-job-first / fair-share) ---
  // Each epoch, rank cores by last epoch's DRAM consumption and apply a GEOMETRIC
  // prefetch-drop by rank: top consumer 0.5, next 0.25, next 0.125... (x pressure).
  // Comes down hardest on the bandwidth hog, barely touches a victim. Starvation-
  // proof: throttling the top cuts its consumption, so it drops in rank next epoch.
  // Never fully gates (rank-0 max drop 0.5). Cheap: one rerank per epoch, tiny state.
  // BUILT + verified correct (drops ~9% of prefetches at strength 1.8), but DEFAULT
  // OFF: empirically underpowered vs bw_feedback. It can only throttle PREFETCHES,
  // and the worst-case aggressors here are DEMAND-bound (mcf/gms) -- little to cut.
  // Feedback wins by collapsing everyone's lookahead under load. As an add-on to
  // feedback it gives a marginal worst-case-victim boost (xalan 0.42->0.47), neutral
  // elsewhere. Needs a stronger lever / prefetch-BW ranking to earn a default-on slot.
  bool enable_bw_rank = false;
  uint64_t bw_rank_epoch = 16384;      // DRAM accesses per rank-recompute epoch
  double bw_rank_lo = 0.50;            // util below which there's no contention -> no throttle
  double bw_rank_hi = 0.85;            // util at/above which throttle is at full strength
  double bw_rank_strength = 1.0;       // overall multiplier on the geometric drop fractions
  uint64_t global_usefulness_sample = 1024;
  uint64_t region_lifespan_sample = 256;
  uint64_t pattern_usefulness_sample = 256;

  // Prefetch placement: the first `prefetch_to_l2_degree` prefetches of a trigger
  // fill L2; any beyond that fill the LLC only (matters for timeliness).
  uint64_t prefetch_to_l2_degree = 1000;
  // Dynamic L2-vs-LLC placement (orig SPPAM behavior): fill L2 only up to the free MSHR/PQ headroom
  // (max(mshr_size - mshr_occupancy - pq_occupancy, 0)); overflow prefetches fill LLC-only. Avoids
  // thrashing L2 with the whole prefetch stream. When off, the fixed prefetch_to_l2_degree is used.
  bool dynamic_l2_fill = true; // ON by default: fill L2 up to MSHR headroom, overflow to LLC (orig SPPAM
                               // placement). Suite-neutral (+0.08% geomean) but restores old-SPPAM behavior
                               // and captures the DRAM-bound wins (cactus +6%); see [pe-ramp-cactus-dram].
  // Adaptive placement: only apply the LLC-overflow policy above when the region map is thrashing
  // hard. Extreme region thrash (ema >= threshold) is the fingerprint of dense over-prediction that
  // floods L2 (measured: cactus ema~0.92; all placement-losers sit <=0.66), so this rescues the
  // cactus pathology without pushing useful prefetches out of L2 on the workloads that want them.
  // Requires enable_region_thrash_throttle to track the EMA.
  bool dynamic_l2_fill_adaptive = false;
  double dynamic_l2_fill_thrash_min = 0.80;

  // ---- Set-dueling L2->LLC redirect throttle (self-calibrating placement) ----
  // A TINY sample of sets via champsim::msl::categorizer. Category 0 = prefetch-guarded: its
  // prefetches are dropped, so it is the no-prefetch baseline. Category 1 = usefulness-sample: its
  // prefetches are never redirected (always L2), so sppam's EXISTING usefulness tracker keeps
  // sampling L2-resident prefetches even when followers redirect (nothing new is built here -- the
  // drop/usefulness throttle is the existing prob_drop/PE/ip_filter system). Each epoch, if the
  // no-prefetch guard OUT-PERFORMS the whole cache, walk the follower L2->LLC redirect fraction up
  // one step; else down. Compared as normalized RATES (count up/down dueling saturates in the
  // prefetcher's favor). Metric is hit rate OR eviction/fill rate -- the latter proxies the
  // cactus/xalan over-prefetch churn that hit rate can't see. Governs SPPAM/SPP/branch-graph.
  bool enable_set_duel = false;
  uint32_t sd_sample_rate = 128; // TINY sample (2048 sets -> ~16 guard + 16 usefulness); 0 = auto
  uint32_t sd_eval_period = 4096;// demands per epoch (one redirect step)
  double sd_step = 1.0 / 32.0;   // redirect walk step
  double sd_margin = 0.003;      // guard-vs-overall margin before a step (scale depends on metric)
  uint32_t sd_metric = 0;        // 0 = hit rate; 1 = eviction rate; 2 = pollution (evict-useful); 3 = I-POP PE
  uint32_t sd_l2_max = 16;        // max L2 fill depth (per-trigger prefetches to L2 before LLC overflow);
                                  // the ACTION is walking this depth down/up (not a random redirect)
  uint32_t sd_l2_floor = 0;       // min L2 fill depth: floor>=1 keeps the shallowest (timely) prefetch in
                                  // L2 even under heavy throttle, so a capacity-bound stream never loses everything

  // ---- PE-gated ramp + LLC-spill (for accurate, DRAM-bound streams) ----
  // When SPPAM is net-useful (PE = I_UPF - I_POLL - I_LAT > pe_ramp_pe_min) AND its fills are
  // DRAM-bound (avg fill latency > pe_ramp_lat_min -- a pseudo-LLC-miss signal), the LLC can stage
  // the far-ahead prefetches out of DRAM early. So: RAMP aggression (deeper degree + lookahead) and
  // SPILL the dense tail to LLC at the MSHR-availability rate (pf_free_space). Inaccurate (PE<=0) or
  // LLC-served (low latency) streams are excluded. Requires enable_pe_management (PE tracking).
  bool enable_pe_ramp = false;
  double pe_ramp_pe_min = 0.0;         // gate: SPPAM PE strictly above this
  double pe_ramp_lat_min = 100.0;      // gate: avg fill latency above this (DRAM-bound)
  int64_t pe_ramp_degree_add = 4;      // + per-trigger degree when gated
  int64_t pe_ramp_degree_cap = 64;     // cap on the ramped degree
  uint32_t pe_ramp_lookahead_add = 4;  // + lookahead depth when gated

  // ---- Adaptive ip_filter via set-dueling (measures OUTCOME, not the misleading usefulness metric) ----
  // The per-IP usefulness filter helps some workloads (xalan) and chokes others (mcf/triangle) where
  // aggression pays despite low per-IP usefulness. Duel it: sample sets run filter-OFF (cat 0) vs
  // filter-ON (cat 1); followers apply the filter only when the filter-ON sample demand-hits MORE than
  // the filter-OFF sample (filter is net-helping). Works here because filter on/off changes L2 residency,
  // so demand hit rate reflects it -- unlike the L2-vs-LLC placement duel. Needs enable_ip_filter.
  bool enable_ipf_duel = false;
  uint32_t ipf_sample_rate = 64;   // tiny sample (0 = auto via msl::get_sample_rate)
  double ipf_margin = 0.002;       // hit-rate margin (hysteresis) before flipping on a low-hit-rate workload
  // Keep-if-safe rule: if the filter BARELY moves a SIGNIFICANT hit rate, keep it on -- there is reuse to
  // protect and marginal aggression isn't worth the pollution risk (xalan). Only when the filter clearly
  // helps hits (big margin: mcf) OR the hit rate is low enough that aggression is safe (triangle) do we
  // disable. Two thresholds on the hit rates we already measure -- no latency/pollution tracking needed.
  double ipf_barely = 0.05;        // |off-on| below this = filter "barely touches" hit rate
  double ipf_significant = 0.35;   // filter-on LEADER-SET hit rate at/above this = "significant" (reuse worth
                                   // protecting). Calibrated to the leader-set scale (~0.4 on xalan), NOT the
                                   // global cumulative hit rate -- the duel measures a 1/64 sample, not the cache.
  // Continuous, no-boundary epochs (real hardware has no warmup/sim line). Each epoch = ipf_eval_period
  // demands. Leaders are live during a MEASUREMENT epoch, then dropped for the next ipf_duty-1 COMMIT epochs
  // where every set follows the decision (no filter-off leaders polluting xalan), then re-measured -- so it
  // keeps adapting forever. CRITICAL: the epoch must be WIDE enough for a leader set (re)filled under the
  // committed policy during commit epochs to RE-WARM under its own policy before we read it, else a narrow
  // probe reads a mid-flip transient (mcf's filter-off measured BELOW filter-on at 8192). ipf_duty=1 =
  // continuous duel (leaders always live, always warm -- most accurate, but permanent leader overhead).
  uint32_t ipf_eval_period = 262144; // demands per epoch (WIDE: leader re-warm >> narrow probe)
  uint32_t ipf_duty = 4;             // measurement epoch once per this many epochs (1 = continuous)

  // --- Cache / memory model (default = DPC4 L2C/LLC) ---
  std::size_t l2_sets = 2048;
  std::size_t l2_ways = 16;
  std::size_t llc_sets = 4096;     // per-core base; shared LLC scales with cores
  std::size_t llc_ways = 12;
  bool llc_scale_with_cores = true; // shared LLC capacity grows proportionally with #cores
  uint64_t l2_hit_latency = 10;
  uint64_t llc_hit_latency = 35;
  bool enable_timing = true;     // false -> prefetches are instantly ready (binary coverage)
  dram_params dram;

  // --- Input handling ---
  // Model berti's L1 prefetches as the PHYSICAL events they are: they fetch from
  // DRAM (bandwidth) and fill L2, whether or not we run an L2 prefetcher. Must be set
  // for BOTH a config and its no-pref baseline, else the baseline runs in an unphysical
  // low-bandwidth world (berti traffic erased) and any comparison is meaningless.
  // Independent of train_on_prefetch (whether our PREDICTOR also learns from them).
  bool simulate_l1_prefetch = false;
  bool train_on_prefetch = false;        // feed is_prefetch records to the predictor
  bool count_prefetch_as_demand = false; // is_prefetch records count as demands/coverage
  bool include_translation = false;      // TRANSLATION records count as demands
  bool include_writeback = true;         // WRITE records update L2 occupancy (never demands)

  uint64_t blocks_per_region() const { return (uint64_t{1} << region_bits) / 64; }
  uint64_t effective_default_prediction() const { return default_prediction ? default_prediction : (uint64_t{1} << (pattern_size - 1)); }

  // Hardware storage estimate (bits), ported from the original SPPAM
  // get_state_bits() accounting (assuming a 48-bit address space). Lets every
  // configuration be checked against a realistic L2-prefetcher budget.
  // Per-term hardware storage breakdown (bits). Every PERSISTENT structure the predictor allocates is
  // accounted here, gated by the SAME flags that allocate it in the constructor, so the total is exact for
  // ANY configuration -- not a byte unaccounted. Diagnostic-only structures (region-density report, exact
  // shadow mirror) are deliberately excluded: they are measurement instruments, not proposed hardware.
  struct state_terms {
    uint64_t region = 0, staging = 0, pattern = 0, cpt = 0, llc = 0, misc = 0, spp = 0, mgmt = 0,
             instr = 0, am = 0, ipf = 0, ip_dir = 0, cold = 0, dpht = 0, xpage = 0, rbloom = 0, rstage = 0, perc = 0;
    uint64_t total() const
    {
      return region + staging + pattern + cpt + llc + misc + spp + mgmt + instr + am + ipf + ip_dir
           + cold + dpht + xpage + rbloom + rstage + perc;
    }
  };

  state_terms state_breakdown() const
  {
    auto lg2 = [](uint64_t x) { uint64_t r = 0; while (x > 1) { x >>= 1; ++r; } return r; };
    const uint64_t bpr = blocks_per_region();
    const uint64_t off_bits = lg2(bpr);
    state_terms t;

    // ---- Region table (main): every per-entry field of region_type, each gated exactly as it is allocated/used.
    uint64_t rpe = region_tag_bits;                                        // hashed short tag
    rpe += account_shadow_links ? (2 * region_tag_bits + 2) : 0;           // next/prev vpn tags + has_next/has_prev
    rpe += bpr * 2;                                                        // access_map + prefetch_map
    rpe += 4;                                                             // momentum (saturating direction)
    rpe += off_bits;                                                      // last_block (last touched offset)
    rpe += scrape_on_idle ? lg2(scrape_idle_time) : 0;
    rpe += scrape_on_count ? lg2(scrape_access_count) : 0;
    rpe += lg2(region_ways);                                             // lru age
    rpe += (pattern_pc_bits > 0) ? static_cast<uint64_t>(pattern_pc_bits) : 0;               // pc_pht: per-region PC context
    rpe += (pattern_context_bits > 0) ? static_cast<uint64_t>(pattern_context_bits) : 0;     // pat_ctx
    rpe += (enable_cold_start && cold_start_pc) ? lg2(cold_start_entries) : 0;                // entry_ip key
    rpe += (enable_cross_page || cross_page) ? (region_tag_bits + 1) : 0;                     // xkey (hashed) + xkey_set
    rpe += (region_evict_policy == 6) ? 24 : 0;                          // pf_used + pf_dead (12b each)
    rpe += (region_evict_policy == 7) ? 12 : 0;                          // dmiss
    rpe += delta_pht ? bpr : 0;                                          // delta_map usefulness bits
    rpe += pattern_perceptron ? (bpr * 8) : 0;                           // block_ip per-block PC hash
    if (enable_spp && spp_share_region_table)
      rpe += off_bits + spp_sig_bits;                                    // SPP last_offset + signature folded in
    t.region = rpe * region_sets * region_ways;
    if (enable_two_level)
      t.staging = rpe * stage_sets * stage_ways;                         // staging entries are region_type too

    // ---- Pattern table(s): TAGGED set-assoc, one per prediction order, x2 for a separate negative table.
    // Per entry: tag (key bits above the set index) + prediction (per-bit counters or the conf table) +
    // occurrence count + optional usefulness + optional perceptron weights + lru.
    uint64_t n_orders = 1;
    for (uint32_t s = pattern_size; s > min_pattern_size && s > 0; s /= 2) ++n_orders;
    const uint64_t key_bits = pattern_size + pattern_context_bits + pattern_pc_bits;
    const uint64_t set_idx_bits = lg2(pattern_table_sets);
    const uint64_t tag_bits = key_bits > set_idx_bits ? key_bits - set_idx_bits : 1; // ~0 only when sets==keyspace
    const uint64_t pred_bits = table_or_counter ? (pattern_size * 7)
                             : (pattern_conf_ways * pattern_conf_sets) * (pattern_size + 7 + lg2(pattern_conf_ways));
    const uint64_t use_bits = (adaptive_usefulness || global_or_pattern_usefulness) ? (lg2(pattern_usefulness_sample) * 2 + 4) : 0;
    const uint64_t pw_bits = pattern_perceptron ? (static_cast<uint64_t>(pattern_size) * static_cast<uint64_t>(pp_hist_bits < 0 ? 0 : pp_hist_bits) * 8) : 0;
    const uint64_t occ_bits = 12;
    const uint64_t per_entry = tag_bits + pred_bits + occ_bits + use_bits + pw_bits + lg2(pattern_table_ways);
    t.pattern = per_entry * pattern_table_sets * pattern_table_ways * n_orders;
    if (do_negative && separate_negative_tables) {          // separate backward table at its own (sizable-down) geometry
      const uint64_t nsets = negative_table_sets ? negative_table_sets : pattern_table_sets;
      const uint64_t nways = negative_table_ways ? negative_table_ways : pattern_table_ways;
      const uint64_t ntag = key_bits > lg2(nsets) ? key_bits - lg2(nsets) : 1;
      t.pattern += (ntag + pred_bits + occ_bits + use_bits + pw_bits + lg2(nways)) * nsets * nways * n_orders;
    }

    // ---- Cross-page tracker + shadow LLC-residency map (both hashed short tags).
    t.cpt = (8 + 1 + region_tag_bits) * cpt_sets * cpt_ways;
    t.llc = llc_region_sets * llc_region_ways * (region_tag_bits + bpr);

    // ---- Misc global counters (+ region-thrash re-reference tag ring).
    t.misc = (lg2(global_usefulness_sample) * 2 + 4) + (lg2(region_lifespan_sample) * 2 + 4) + (4 + 4 + 8);
    if (enable_region_thrash_throttle) t.misc += 16 + region_thrash_table * region_tag_bits; // EMA reg + tag ring (rev_tags_)

    // ---- SPP-lite delta prefetcher: PT + ST (or region-shared) + optional per-sig filter + optional GHR.
    if (enable_spp) {
      const uint64_t delta_bits = 7, conf_bits = spp_conf_bits;
      uint64_t per_way = spp_sig_bits + lg2(spp_pt_ways) + spp_deltas_per_sig * (delta_bits + conf_bits);
      uint64_t per_sig_state = 0;
      if (spp_per_sig_usefulness) {
        per_way += 2 * spp_usefulness_bits;
        per_sig_state = spp_pf_filter_entries * (16 /*partial block tag*/ + spp_sig_bits + 1);
      }
      t.spp = spp_pt_sets * spp_pt_ways * per_way + per_sig_state;
      if (!spp_share_region_table) {                                     // shared case already added last_offset+sig to region
        const uint64_t st_tag = 48 - lg2(region_bits);
        t.spp += spp_st_entries * (st_tag + off_bits + spp_sig_bits);
      }
      if (spp_ghr) t.spp += spp_ghr_entries * (spp_sig_bits + off_bits + 1); // ghr_ (sig + landing offset + valid)
    }

    // ---- Bandwidth/PE management overhead (per-L2-line source tags + PE counters + feedback/market regs).
    uint64_t mgmt = 0;
    if (enable_pe_management) {
      // NB: NO per-L2-line source tags. The source (from_spp) and DRAM-ness (inferred from the fill latency) are read
      // from the pfht_ SAMPLING table below -- that is the whole point of sampling. Only the per-source PE counters.
      mgmt += 2 * (32 + 32 + 32 + 32 + lg2(pe_throttle_div + 1)) + lg2(pe_phase);
    }
    // pfht_ (sampled prefetch-outcome tracker) + poll_ (pollution victims): the sampling table the whole design leans
    // on for PE + per-IP attribution. SHARED by PE-management and the ip-filter; allocated whenever either is on.
    if (enable_pe_management || enable_ip_filter) {
      const uint64_t iph_b = lg2(ip_table_entries > 1 ? static_cast<uint64_t>(ip_table_entries) : 2);
      mgmt += static_cast<uint64_t>(pfht_entries) * (1 + pfht_tag_bits + 1 + 16 + 1 + 17 + iph_b);  // pf_track: valid+tag+from_spp+issue+filled+lat+iph
      mgmt += static_cast<uint64_t>(pfht_entries) * (1 + pfht_tag_bits + pfht_tag_bits + 1 + iph_b); // poll_track: valid+victim-tag+pf-tag+from_spp+iph
    }
    if (enable_bw_feedback) mgmt += 16 * 5 + 16;
    if (enable_bw_market) mgmt += 16;
    mgmt += 2 * 48;                                                      // shared: L_DRAM running mean + in-flight register
    t.mgmt = mgmt;

    // ---- Branch-graph instruction prefetcher: tagged 2-delta edge table + vpage->ppage xlate + residency filter.
    if (enable_instr_prefetch) {
      auto po2 = [](uint64_t n) { uint64_t r = 1; while (r < n) r <<= 1; return r; };
      const uint64_t db = (instr_delta_bits > 0 && instr_delta_bits <= 16) ? instr_delta_bits : 8;
      const uint64_t per_edge = 16 + 1 + 2 * db + 2 * 4;
      t.instr = po2(instr_table_entries) * per_edge + po2(instr_xlate_entries) * (36 + 36);
      if (!instr_packed_residency)                                       // packed residency reuses SPPAM's maps -> no private filter
        t.instr += po2(instr_filter_entries) * 16;
    }

    // ---- Access-map bloom.
    if (enable_am_bloom)
      t.am = bpr * (static_cast<uint64_t>(am_bloom_size) + lg2(am_bloom_clear_thresh) + lg2(am_bloom_size));

    // ---- IP-filter tables: per-IP throttle counters + sampled block->{pattern,IP} attribution + validation.
    {
      const uint64_t E = ip_table_entries ? ip_table_entries : 1;
      const uint64_t iph_bits = lg2(E);
      const uint64_t samp_entry = 16 /*block tag*/ + iph_bits + lg2(pattern_size + 2) /*position*/ + 1 /*bwd*/
                                + (pv_sample_directmap ? (8 /*stamp*/ + 1 /*occ*/) : 0);
      if (enable_ip_filter) {
        uint64_t arrays = 3;                                            // useful + useless + gate_ctr
        if (ip_filter_depth_throttle) arrays += 2;                      // ev + untimely
        if (bwd_useful_gate) arrays += 2;                               // bwd_useful + bwd_useless
        t.ipf += arrays * E * ip_ctr_bits;
      }
      if (enable_ip_filter || pattern_validate)                         // pf_sample_ shared by filtering & validation
        t.ipf += pv_sample_cap * samp_entry;
      if (enable_ip_filter && ip_filter_depth_throttle)                 // evicted_unused_ re-demand watch list (block->IP)
        t.ipf += evicted_unused_cap * (16 + iph_bits);
      if (pattern_validate) {                                           // pat_val_ (u,n) keyed by the pattern key
        const uint64_t pv_entries = (key_bits < 20) ? (uint64_t{1} << key_bits) : (uint64_t{1} << 20);
        t.ipf += pv_entries * (2 * 12);
      }
    }

    // ---- Per-IP stride-direction table (fixed 256 x int8; feeds neg_dir_pc / ip_direction).
    if (ip_direction || neg_dir_pc) t.ip_dir = 256 * 8;

    // ---- Cold-start PC-keyed delta table (delta + confidence).
    if (enable_cold_start && cold_start_pc) t.cold = cold_start_entries * (8 + 4);

    // ---- Delta-PHT (spatial-context -> signed delta) + per-region speculative/walk overlays.
    if (delta_pht) {
      t.dpht = static_cast<uint64_t>(delta_pht_sets) * delta_pht_ways * (16 /*tag*/ + 8 /*delta*/ + spp_conf_bits) + bpr;
      if (walk_accumulate) t.dpht += 3 * bpr;                           // walk_spec_/issued_/acc_ overlays
    }

    // ---- Cross-page value table (xpht_): per key a bpr-wide count vector + occ (bounded like the tracker).
    if (enable_cross_page || cross_page)
      t.xpage = static_cast<uint64_t>(cpt_sets) * cpt_ways * (region_tag_bits + bpr * 8 + 12);

    // ---- Residency bloom + region-staging spatial gate.
    if (enable_resid_bloom) t.rbloom = resid_bloom_bits;
    if (enable_region_staging) t.rstage = staging_entries * (16 /*tag*/ + 8 /*count*/);

    // ---- Perceptron prefetch filter (optional): per-feature weight tables + per-outstanding-prefetch training buffers.
    if (enable_perceptron_filter) {
      auto po2 = [](uint64_t n) { uint64_t r = 1; while (r < n) r <<= 1; return r; };
      const uint64_t wbits = lg2(2 * (perc_weight_max > 0 ? static_cast<uint64_t>(perc_weight_max) : 1) + 1) + 1; // signed weight
      const uint64_t ctxm = perc_engine_split ? 4 : 1; // context tables (dep,use,tim,conf,mshr,guse,gtim,hit) are per-engine when split
      const uint64_t entries = po2(perc_pc_entries) + 4 + ctxm * (16 + 8 + 8 + 16 + 16 + 16 + 16 + 16) + po2(perc_sig_entries) + 256 + 2 * po2(perc_pc_entries)
                             + 128 + 128 + 256 + 128; // + PC-path + PC^delta + 4 DSE interaction tables (use^mshr, tim^mshr, conf^mshr, criticality); offset dropped
      t.perc = entries * wbits;
      // ONE unified sampling table (perc_track_): feature snapshot + fill-latency + accrued cost (perc_sdm_ removed).
      t.perc += po2(perc_track_cap) * (19 * 10 + 16 + 16 + 16 + 1 + 1 + 17 + 16 + 1); // 19 feat (~10b) + tag+stamp+iph+occ+explored + lat(17b)+cost(~16b)+filled
      if (!enable_ip_filter) t.perc += 2ull * ip_table_entries * 16; // per-IP usefulness (ip_useful_/ip_useless_) owned by the perceptron when the ip-filter is off
      t.perc += static_cast<uint64_t>(ip_table_entries) * wbits; // per-IP bias (pip_bias_): one signed weight per IP
    }

    return t;
  }

  uint64_t state_bits() const { return state_breakdown().total(); }
  double state_kib() const { return static_cast<double>(state_bits()) / 8.0 / 1024.0; }

  // Human-readable per-term breakdown in KiB (for budget audits). Terms are omitted when zero.
  std::string state_report() const
  {
    const state_terms t = state_breakdown();
    auto kib = [](uint64_t bits) { const uint64_t x = (bits * 10 + 4096) / 8192; return std::to_string(x / 10) + "." + std::to_string(x % 10); };
    std::string s = "state " + kib(t.total()) + " KiB =";
    auto add = [&](const char* n, uint64_t b) { if (b) s += " " + std::string(n) + "=" + kib(b); };
    add("region", t.region); add("staging", t.staging); add("pattern", t.pattern); add("cpt", t.cpt);
    add("llc", t.llc); add("misc", t.misc); add("spp", t.spp); add("mgmt", t.mgmt); add("instr", t.instr);
    add("am", t.am); add("ipf", t.ipf); add("ip_dir", t.ip_dir); add("cold", t.cold); add("dpht", t.dpht);
    add("xpage", t.xpage); add("rbloom", t.rbloom); add("rstage", t.rstage); add("perc", t.perc);
    return s;
  }
};

// Overrides any subset of fields present in `j`.
inline void apply_json(params& p, const nlohmann::json& j)
{
#define SET(field) if (j.contains(#field)) p.field = j.at(#field).get<decltype(p.field)>()
  SET(name);
  SET(page_bits); SET(region_bits); SET(region_sets); SET(region_ways); SET(llc_region_sets); SET(llc_region_ways); SET(region_hash); SET(dedup_analysis); SET(account_shadow_links); SET(within_page_shadow); SET(region_page_aligned_sets);
  SET(enable_shadow_squash); SET(exact_shadow_test); SET(enable_hybrid_bidding); SET(bid_by_value); SET(bid_explore_div);
  SET(enable_resid_bloom); SET(resid_bloom_bits); SET(resid_bloom_k); SET(resid_bloom_clear);
  SET(enable_am_bloom); SET(am_bloom_size); SET(am_bloom_k); SET(am_bloom_clear_thresh); SET(am_bloom_clear_frac);
  SET(enable_ip_filter); SET(ip_filter_threshold); SET(ip_filter_min_samples); SET(ip_filter_trickle); SET(ip_filter_age_shift);
  SET(ip_filter_threshold_hard); SET(ip_filter_trickle_hard); SET(ip_filter_use_pe); SET(ip_pe_hard_frac);
  SET(ip_filter_pe_veto); SET(ip_pe_veto_frac);
  SET(ip_filter_use_pe_phase); SET(ip_pe_phase_soft); SET(ip_pe_phase_hard); SET(ip_pe_phase_margin);
  SET(ip_filter_depth_throttle); SET(ip_depth_mid); SET(ip_depth_min); SET(ip_untimely_thresh); SET(ip_depth_hitrate_min); SET(ip_depth_mlp_max);
  SET(ip_filter_max_useful_loss); SET(ip_sample_div); SET(ip_track_timeout);
  SET(ip_table_entries); SET(ip_ctr_bits); SET(evicted_unused_cap);
  SET(enable_fallthrough); SET(fallthrough_explore_div);
  SET(enable_spp); SET(spp_st_entries); SET(spp_sig_bits); SET(spp_lookahead); SET(spp_threshold); SET(spp_share_region_table); SET(spp_usefulness_feedback);
  SET(spp_ghr); SET(spp_ghr_entries); SET(spp_min_delta); SET(spp_min_conf); SET(spp_multi_high_throttle);
  SET(spp_per_sig_usefulness); SET(spp_pf_filter_entries); SET(spp_pf_sample_div); SET(spp_usefulness_bits); SET(spp_per_sig_floor); SET(spp_per_sig_prior);
  SET(spp_pt_sets); SET(spp_pt_ways); SET(spp_deltas_per_sig); SET(spp_conf_bits); SET(region_tag_bits);
  SET(pattern_size); SET(min_pattern_size); SET(pattern_context_bits); SET(pattern_pc_bits); SET(pattern_context_src); SET(table_or_counter);
  SET(min_confidence_to_prefetch); SET(counter_up); SET(counter_down);
  SET(online_learning); SET(online_neg_samples); SET(online_theta_train);
  SET(pattern_perceptron); SET(pp_hist_bits); SET(pp_pc_bits); SET(pp_weight_cap);
  SET(pattern_table_sets); SET(pattern_table_ways); SET(negative_table_sets); SET(negative_table_ways);
  SET(pattern_conf_sets); SET(pattern_conf_ways); SET(cpt_sets); SET(cpt_ways);
  SET(scrape_on_idle); SET(scrape_idle_time); SET(scrape_on_count);
  SET(scrape_min_count); SET(scrape_access_count); SET(scrape_on_evict);
  SET(mark_after_scrape); SET(clear_after_scrape); SET(clear_filter_after_scrape); SET(scrape_full_window);
  SET(access_map_miss_only); SET(train_demand_only);
  SET(do_negative); SET(separate_negative_tables);
  SET(neg_online_train); SET(neg_train_gated); SET(neg_dir_pc);
  SET(bwd_useful_gate); SET(bwd_useful_thresh); SET(bwd_useful_min_samples);
  SET(scan_forward); SET(scan_distance_forward); SET(scan_backward); SET(scan_distance_backward);
  SET(delta_pht); SET(delta_sig_radius); SET(delta_pht_sets); SET(delta_pht_ways);
  SET(delta_pht_conf_max); SET(delta_pht_conf_min); SET(delta_pht_degree); SET(delta_pht_max); SET(delta_pht_path_conf);
  SET(delta_pht_selfthrottle); SET(delta_pht_min_acc); SET(delta_pht_max_unused); SET(delta_pht_max_hitrate); SET(delta_pht_warmup); SET(delta_pht_explore); SET(delta_pht_ema_shift);
  SET(delta_pht_setduel); SET(delta_pht_sd_period); SET(delta_pht_sd_max);
  SET(enable_cold_start); SET(cold_start_degree); SET(cold_start_pc); SET(cold_start_entries);
  SET(enable_cross_page); SET(cross_page_degree); SET(cross_page_conf); SET(cross_page_min_occ); SET(cross_page_key);
  SET(region_evict_policy);
  SET(enable_two_level); SET(stage_sets); SET(stage_ways); SET(stage_promote_min); SET(stage_promote_max);
  SET(forward_momentum_min); SET(backward_momentum_min); SET(cross_page);
  SET(ip_direction); SET(ip_direction_min); SET(use_berti_src_ip); SET(pollution_filter);
  SET(region_density_report);
  SET(do_lookahead); SET(lookahead_depth); SET(lookahead_conf_cutoff); SET(lookahead_conf_factor);
  SET(walk_accumulate); SET(walk_max_depth); SET(walk_thresh_base); SET(walk_thresh_slope); SET(walk_degree);
  SET(walk_thresh_cap); SET(walk_jump_relief); SET(walk_advance); SET(walk_usefulness_throttle); SET(walk_replace); SET(walk_fallthrough); SET(walk_ft_usefulness); SET(walk_setduel);
  SET(pattern_validate); SET(pv_sample_div); SET(pv_min_samples); SET(pv_bad_pct); SET(pv_sample_cap);
  SET(pv_sample_directmap); SET(pv_sample_ttl); SET(pv_sample_evict_div);
  SET(pv_adaptive_rate); SET(pv_rate_window); SET(pv_churn_hi); SET(pv_churn_lo); SET(pv_div_min); SET(pv_div_max);
  SET(enable_perceptron_filter); SET(perc_pc_entries); SET(perc_weight_max); SET(perc_tau_keep);
  SET(perc_theta_train); SET(perc_explore_div); SET(perc_label_pe); SET(perc_pe_margin); SET(perc_track_cap); SET(perc_track_ttl); SET(perc_pe_scale); SET(perc_pe_step_max); SET(perc_feat_mask); SET(perc_pe_signonly); SET(perc_sig_entries); SET(perc_pe_norm); SET(perc_pe_cost); SET(perc_untimely_veto); SET(perc_load_gate); SET(perc_load_gate_thresh); SET(perc_instr_gate); SET(perc_instr_gate_pct); SET(perc_pc_encoding); SET(perc_pc_lobit); SET(perc_pc_dropmask); SET(perc_dump_weights); SET(perc_gate_instr); SET(perc_engine_split); SET(perc_profile);
  SET(pv_feed_confidence); SET(pv_conf_penalty);
  SET(prob_drop_prefetches); SET(global_or_pattern_usefulness); SET(adaptive_usefulness);
  SET(pattern_usefulness_cutoff); SET(use_default_prediction); SET(default_pattern); SET(default_prediction);
  SET(prefetch_demand_only); SET(prefetch_degrees_usefulness); SET(prefetch_drop_chance_usefulness);
  SET(global_usefulness_sample); SET(region_lifespan_sample); SET(pattern_usefulness_sample);
  SET(enable_bw_feedback); SET(bw_mult); SET(prefetch_degrees_bw);
  SET(enable_bw_market); SET(bw_market_target_util); SET(bw_market_step); SET(market_trickle_div);
  SET(enable_bw_rank); SET(bw_rank_epoch); SET(bw_rank_lo); SET(bw_rank_hi); SET(bw_rank_strength);
  SET(enable_region_staging); SET(staging_entries); SET(staging_promote_threshold);
  SET(enable_ip_gate); SET(ip_gate_div_min); SET(ip_gate_explore_div);
  SET(gate_adaptive); SET(gate_thrash_min);
  SET(enable_instr_prefetch); SET(instr_la_depth); SET(instr_conf); SET(instr_table_entries); SET(instr_delta_bits); SET(instr_xlate_entries); SET(instr_filter_entries);
  SET(instr_feed_data); SET(unblock_instructions);
  SET(instr_nextn); SET(instr_packed_residency);
  SET(enable_region_thrash_throttle); SET(region_thrash_min_blocks); SET(region_thrash_table); SET(region_thrash_lo); SET(region_thrash_hi); SET(region_thrash_max_drop);
  SET(enable_pe_management); SET(pe_phase); SET(pe_throttle_div); SET(pfht_entries); SET(pfht_tag_bits); SET(pe_sample_div); SET(pe_pf_demand_weight);
  SET(pe_serv_dram); SET(pe_serv_llc); SET(pe_dram_lat_threshold);
  SET(prefetch_to_l2_degree); SET(dynamic_l2_fill); SET(dynamic_l2_fill_adaptive); SET(dynamic_l2_fill_thrash_min);
  SET(enable_set_duel); SET(sd_sample_rate); SET(sd_eval_period); SET(sd_step); SET(sd_margin); SET(sd_metric); SET(sd_l2_max); SET(sd_l2_floor);
  SET(enable_pe_ramp); SET(pe_ramp_pe_min); SET(pe_ramp_lat_min); SET(pe_ramp_degree_add); SET(pe_ramp_degree_cap); SET(pe_ramp_lookahead_add);
  SET(enable_ipf_duel); SET(ipf_sample_rate); SET(ipf_eval_period); SET(ipf_margin); SET(ipf_barely); SET(ipf_significant); SET(ipf_duty);
  SET(l2_sets); SET(l2_ways); SET(llc_sets); SET(llc_ways); SET(llc_scale_with_cores);
  SET(l2_hit_latency); SET(llc_hit_latency); SET(enable_timing);
  SET(simulate_l1_prefetch); SET(train_on_prefetch); SET(count_prefetch_as_demand); SET(include_translation); SET(include_writeback);
#undef SET
  if (j.contains("dram")) {
    const auto& d = j.at("dram");
    if (d.contains("base_latency")) p.dram.base_latency = d.at("base_latency").get<double>();
    if (d.contains("service_cycles")) p.dram.service_cycles = d.at("service_cycles").get<double>();
    if (d.contains("tau")) p.dram.tau = d.at("tau").get<double>();
    if (d.contains("util_cap")) p.dram.util_cap = d.at("util_cap").get<double>();
    if (d.contains("max_latency")) p.dram.max_latency = d.at("max_latency").get<double>();
  }
}

} // namespace sppam_dse

#endif
