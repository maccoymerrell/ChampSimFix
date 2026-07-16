// pc_sppam_eval: clean coverage/accuracy DSE for PC-context SPPAM prediction.
//
// Pure prediction model -- NO cache / latency / bandwidth / throttling. Isolates the
// prediction ENGINE's coverage + accuracy so we can find the minimal state that maximizes
// them and the policy that switches between mechanisms. Everything shares ONE trace replay
// and ONE region access-map + load-PC branch graph; each named engine keeps its own PHT and
// scoring, so their numbers are directly comparable.
//
// The SPPAM core (faithful to the shipping engine, throttling stripped):
//   - Region = 4 KiB page, 64 blocks; a per-page access map (bits) is the spatial state.
//   - Spatial "window" at position p = the ps blocks ahead [p+1..p+ps], MSB=p+1 (patterns_at).
//   - Online training (per demand, on the map state BEFORE this access is marked): for each
//     distance d in [1..ps], PHT[ window_at(off-d), ctx ] votes that a block is d ahead
//     (target bit ps-d). ctx = (history) | (PC), the PC being the PREVIOUS accessing PC in
//     the page (pc_pht), read before it is updated to the current PC -- so train keys an
//     access on the PC that PRECEDED it.
//
// Prediction knobs (the design space):
//   context   nopc | pc         -- fold pc_bits of the accessing PC into the PHT key.
//   nav       scan | single     -- scan = SPPAM's multi-start walk (no PC; non-accessed
//                                  blocks have no PC context). single = one deep trajectory,
//                                  advancing along the strongest delta, PC advanced by the
//                                  load-PC branch graph (future PC), window taken from a
//                                  speculatively-marked map so position lines up with the PC.
//   encoding  bitmap | deltaK   -- bitmap uses all bits above conf; deltaK uses only the top-K
//                                  predicted deltas (the compact, state-cheap encoding).
//   issue     perstep | accum   -- perstep issues each step's predictions directly; accum sums
//                                  path_conf*pred_conf per absolute position over the whole walk
//                                  and issues where the accumulated weight >= issue_thresh.
//
// Coverage/accuracy via a within-W horizon oracle (no cache): a prediction is accurate if its
// block is demanded within the next W accesses; a demand is covered if some earlier prediction
// targeted its block within W.  State is reported per engine (bitmap vs deltaK).
//
// WORKS ALONGSIDE BERTI (not optional). Use the berti_plus l1d_l2c trace: its LOAD/RFO records
// are the RESIDUAL demand misses berti (at L1D) left for the L2 to cover, and its PREFETCH
// records are berti's L2-bound fills. SPPAM's job is that residual. We track berti's prefetches
// to report (a) redundancy -- a SPPAM prefetch of a block berti is already fetching is wasted --
// and (b) incremental coverage -- residual demands SPPAM covers that berti's L2 prefetches don't.
//
// Build: make pc_sppam_eval        Run: ./pc_sppam_eval --trace T.berti_plus.l1d_l2c.bin.zst [opts]
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <nlohmann/json.hpp>

#include "access_kind.h"
#include "trace_reader.h"
#include "iprefetch_predictor.h"

using namespace sppam_dse;

namespace {

constexpr int BPP = 64;      // blocks per 4 KiB page
static inline uint64_t Hh(uint64_t x){ x^=x>>33; x*=0xff51afd7ed558ccdull; x^=x>>33; return x; }
static inline uint64_t mask(uint32_t b){ return b>=64?~uint64_t{0}:((uint64_t{1}<<b)-1); }

// ---- load-PC branch graph: pc-block -> next pc-block (top-2 deltas, saturating) ----
struct BranchGraph {
  struct E{ uint32_t tag=0; bool v=false; int64_t d0=0,d1=0; uint32_t c0=0,c1=0; };
  std::vector<E> t; uint64_t m; int order;
  BranchGraph(std::size_t n,int ord):t(n),m(n-1),order(ord){}
  E& slot(uint64_t ib){ E&e=t[Hh(ib)&m]; uint32_t tg=(uint32_t)(Hh(ib>>20)&0xffffffu);
    if(!e.v||e.tag!=tg){e=E{};e.tag=tg;e.v=true;} return e; }
  void train(uint64_t f,uint64_t to){ int64_t d=(int64_t)to-(int64_t)f; if(d>=512||d<=-512)return; E&e=slot(f);
    if(e.c0&&e.d0==d)++e.c0; else if(e.c1&&e.d1==d)++e.c1; else if(!e.c0){e.d0=d;e.c0=1;}
    else if(!e.c1){e.d1=d;e.c1=1;} else{ if(e.c1<e.c0){e.d1=d;e.c1=1;} else {e.d0=d;e.c0=1;} }
    if(e.c1>e.c0){std::swap(e.c0,e.c1);std::swap(e.d0,e.d1);} }
  uint64_t predict(uint64_t f) const { const E&e=t[Hh(f)&m]; uint32_t tg=(uint32_t)(Hh(f>>20)&0xffffffu);
    if(!e.v||e.tag!=tg||!e.c0) return f; return (uint64_t)((int64_t)f+e.d0); }
  // read the trained edge for key f: returns false if cold (no edge); else fills the two successors.
  bool slot_read(uint64_t f,int64_t&d0,int64_t&d1,uint32_t&c0,uint32_t&c1) const {
    const E&e=t[Hh(f)&m]; uint32_t tg=(uint32_t)(Hh(f>>20)&0xffffffu);
    if(!e.v||e.tag!=tg||!e.c0) return false; d0=e.d0;d1=e.d1;c0=e.c0;c1=e.c1; return true; }
  std::size_t state_bits() const { std::size_t n=0; for(auto&e:t) if(e.v)++n;
    return n*(24 /*tag*/ + 2*(10/*delta*/+4/*cnt*/)); }
};

// ---- PHT: key -> ps saturating access counters + occurrence count ----
struct Row { std::vector<uint16_t> cnt; uint32_t occ=0; };
struct PHT {
  std::unordered_map<uint64_t,Row> t; int ps;
  explicit PHT(int p):ps(p){}
  void train(uint64_t key,int bit){ if(bit<0||bit>=ps)return; auto&r=t[key];
    if(r.cnt.empty())r.cnt.assign(ps,0); ++r.occ; if(r.cnt[bit]<65535)++r.cnt[bit]; }
  // returns predicted (delta, conf) pairs; conf = cnt/occ. bit b -> block at pos + (ps-b).
  // topk>0 keeps only the K strongest; conf_min filters. sorted strongest-first.
  void lookup(uint64_t key,double conf_min,int topk,std::vector<std::pair<int,double>>& out) const {
    out.clear(); auto it=t.find(key); if(it==t.end())return; const Row&r=it->second; if(!r.occ)return;
    for(int b=0;b<ps;++b){ if(!r.cnt[b])continue; double c=(double)r.cnt[b]/r.occ; if(c<conf_min)continue;
      out.push_back({ps-b, c}); }
    std::sort(out.begin(),out.end(),[](auto&a,auto&b){return a.second>b.second;});
    if(topk>0 && (int)out.size()>topk) out.resize(topk);
  }
  // state: bitmap = ps counters/key; deltaK = K (delta,cnt) pairs/key.
  std::size_t state_bits(int keybits,int topk) const {
    std::size_t per = topk>0 ? (std::size_t)topk*(6/*delta*/+8/*cnt*/) : (std::size_t)ps*8;
    return t.size()*(keybits + per + 12/*occ*/);
  }
};

// ---- shared region access-map (physical page) + previous accessing PC ----
struct Region { uint64_t amap=0; uint64_t prev_ip=0; bool seen=false; uint32_t since_scrape=0; };

static inline uint64_t window_at(uint64_t amap,int p,int ps){
  uint64_t w=0; for(int i=1;i<=ps;++i){ int q=p+i; w=(w<<1)|((q>=0&&q<BPP)?((amap>>q)&1):0); } return w;
}

struct EngCfg {
  std::string name;
  int ps=8;
  uint32_t pc_bits=0;         // 0 => nopc
  std::string nav="single";   // scan | single
  std::string train="online"; // online (per-access) | scrape (batch the region map every scrape_period)
  int topk=0;                 // 0 => bitmap (all above conf), K => deltaK encoding
  std::string issue="accum";  // perstep | accum
  double conf_min=0.30;       // per-prediction confidence floor
  double issue_thresh=0.30;   // accumulated-weight floor to issue (accum mode)
  double advance_min=0.25;    // min conf to advance the single path
  int depth=16;               // max lookahead steps (single); scan distance (scan)
  double decay=0.85;          // per-step path-confidence decay for accumulation
};

struct Engine {
  EngCfg c; PHT pht; uint64_t issued=0, correct=0, redun=0;
  std::vector<uint8_t> covered;                 // per-demand covered flag
  Engine(const EngCfg&cfg,uint32_t N):c(cfg),pht(cfg.ps),covered(N,0){}
  int keybits() const { return c.ps + (c.pc_bits?(int)c.pc_bits:0); }
  uint64_t key(uint64_t win,uint64_t ip) const {
    return c.pc_bits ? (win | ((Hh(ip)&mask(c.pc_bits))<<c.ps)) : win;
  }
};

// 2-successor delta edge (for the load-PC -> instruction correlation).
struct LPEdge { int64_t d0=0,d1=0; uint32_t c0=0,c1=0;
  void obs(int64_t d){ if(c0&&d0==d)++c0; else if(c1&&d1==d)++c1; else if(!c0){d0=d;c0=1;}
    else if(!c1){d1=d;c1=1;} else{ if(c1<c0){d1=d;c1=1;} else {d0=d;c0=1;} }
    if(c1>c0){std::swap(c0,c1);std::swap(d0,d1);} } };

// ---- bounded structures for the realizable merged-CF instruction predictor ----
// Edge = SPP-style: code block -> 2 successor DELTAS + 4-bit counts (counts ARE the path-correctness
// confidence -- no usefulness tracking needed). Direct-mapped, tag, evict-on-collision.
struct CFEdges {
  struct E{ uint16_t tag=0; bool v=false; int32_t d0=0,d1=0; uint8_t c0=0,c1=0,run=1; }; // run = BB size (blocks) at the dominant target
  std::vector<E> t; uint64_t mask; int64_t dlim; int dbits;
  CFEdges(int n,int db){ int p=1; while(p<n)p<<=1; t.assign(p,E{}); mask=p-1; dbits=db; dlim=int64_t{1}<<(db-1); }
  static uint16_t tg(uint64_t k){ return (uint16_t)((Hh(k)>>21)&0xFFFF); }
  E* get(uint64_t k){ E&e=t[Hh(k)&mask]; return (e.v&&e.tag==tg(k))?&e:nullptr; }
  void set_run(uint64_t k,int r){ E&e=t[Hh(k)&mask]; if(e.v&&e.tag==tg(k)) e.run=(uint8_t)(r<1?1:(r>15?15:r)); }
  void obs(uint64_t k,int64_t d){ if(d>=dlim||d<=-dlim)return; E&e=t[Hh(k)&mask]; uint16_t g=tg(k);
    if(!e.v||e.tag!=g){e=E{};e.tag=g;e.v=true;}
    if(e.c0&&e.d0==d){if(e.c0<15)++e.c0;} else if(e.c1&&e.d1==d){if(e.c1<15)++e.c1;}
    else if(!e.c0){e.d0=(int32_t)d;e.c0=1;} else if(!e.c1){e.d1=(int32_t)d;e.c1=1;}
    else if(e.c0<=e.c1){if(--e.c0==0){e.d0=(int32_t)d;e.c0=1;}} else if(--e.c1==0){e.d1=(int32_t)d;e.c1=1;}
    if(e.c1>e.c0){std::swap(e.c0,e.c1);std::swap(e.d0,e.d1);} }
  double kib()const{ return t.size()*(16+1+2*(double)dbits+2*4+4/*run*/)/8.0/1024.0; }
};
struct IXlate {
  std::vector<uint64_t> vp; std::vector<uint32_t> pp; uint64_t mask;
  explicit IXlate(int n){ int p=1; while(p<n)p<<=1; vp.assign(p,~uint64_t{0}); pp.assign(p,0); mask=p-1; }
  void ins(uint64_t v,uint32_t g){ std::size_t i=Hh(v)&mask; vp[i]=v; pp[i]=g; }
  bool get(uint64_t v,uint32_t& g)const{ std::size_t i=Hh(v)&mask; if(vp[i]!=v)return false; g=pp[i]; return true; }
  double kib()const{ return vp.size()*(16+24)/8.0/1024.0; }
};
// The L2 cache (set-assoc LRU) with per-line pf/used tags -- ground-truth residency AND prefetch
// coverage (a demand that hits an unused prefetched line is COVERED). install() returns the evicted block.
struct L2Cache {
  static constexpr uint64_t NONE=~uint64_t{0};
  int sets,ways; std::vector<uint64_t> tag; std::vector<uint8_t> val,pf,used,insb; std::vector<uint32_t> lru; uint32_t clk=0;
  L2Cache(int s,int w):sets(s),ways(w),tag((std::size_t)s*w,0),val((std::size_t)s*w,0),pf((std::size_t)s*w,0),used((std::size_t)s*w,0),insb((std::size_t)s*w,0),lru((std::size_t)s*w,0){}
  int findw(uint64_t b)const{ std::size_t base=(std::size_t)(b%sets)*ways; for(int k=0;k<ways;k++) if(val[base+k]&&tag[base+k]==b)return (int)(base+k); return -1; }
  bool resident(uint64_t b)const{ return findw(b)>=0; }
  // install b (pf or demand); returns evicted block (NONE if already resident or victim empty). Optionally
  // tags the line as instruction (is_instr) and reports whether the EVICTED victim was an instruction line.
  uint64_t install(uint64_t b,bool is_pf,bool is_instr=false,bool* victim_instr=nullptr){ std::size_t base=(std::size_t)(b%sets)*ways; int hit=-1,lw=0;
    for(int k=0;k<ways;k++){ if(val[base+k]&&tag[base+k]==b)hit=k; if(!val[base+k]||lru[base+k]<lru[base+lw])lw=k; }
    if(hit>=0){ lru[base+hit]=++clk; return NONE; }
    uint64_t ev=val[base+lw]?tag[base+lw]:NONE; if(victim_instr)*victim_instr=(val[base+lw]&&insb[base+lw]);
    tag[base+lw]=b; val[base+lw]=1; pf[base+lw]=is_pf?1:0; used[base+lw]=0; insb[base+lw]=is_instr?1:0; lru[base+lw]=++clk; return ev; }
  // demand access: returns true iff it hit an UNUSED prefetched line (=> covered). Marks used.
  bool demand_useful(uint64_t b){ int w=findw(b); if(w<0)return false; bool u=pf[w]&&!used[w]; used[w]=1; pf[w]=0; lru[w]=++clk; return u; }
  // demand access reporting prefetch source: -1=miss, 0=hit already-used/demand line, 1=hit UNUSED prefetch
  // (covered); on covered, *src = the fill source stored in insb (0=berti, 1=SPPAM). Marks used.
  int demand_cov(uint64_t b,int* src){ int w=findw(b); if(w<0)return -1; int r=(pf[w]&&!used[w])?1:0; if(r&&src)*src=insb[w]; used[w]=1; pf[w]=0; lru[w]=++clk; return r; }
  // keep-alive touch: re-prefetch hit a resident line -> promote to MRU (no state change otherwise).
  void touch(uint64_t b){ int w=findw(b); if(w>=0) lru[w]=++clk; }
};
// SPPAM's prefetch map: REGION-TAGGED table, BITMAP PER REGION (64 blocks/4KB page). A block's bit is SET
// when the line fills the cache and CLEARED when that LINE is EVICTED from the cache (while the region entry
// is still in the region table). Region entries are LRU-managed; a region evicting drops its whole bitmap.
// SHARED with data (code and data regions coexist), so no instruction-marginal state.
struct RegionMap {
  int sets,ways; std::vector<uint64_t> tag, bits; std::vector<uint8_t> val; std::vector<uint32_t> lru; uint32_t clk=0;
  RegionMap(int s,int w):sets(s),ways(w),tag((std::size_t)s*w,0),bits((std::size_t)s*w,0),val((std::size_t)s*w,0),lru((std::size_t)s*w,0){}
  int find(uint64_t pg)const{ std::size_t base=(std::size_t)(pg%sets)*ways;
    for(int k=0;k<ways;k++) if(val[base+k]&&tag[base+k]==pg) return (int)(base+k); return -1; }
  int touch(uint64_t pg){ int idx=find(pg); if(idx>=0){ lru[idx]=++clk; return idx; }
    std::size_t base=(std::size_t)(pg%sets)*ways; int lw=0;
    for(int k=0;k<ways;k++) if(!val[base+k]||lru[base+k]<lru[base+lw])lw=k;
    int i=(int)(base+lw); tag[i]=pg; bits[i]=0; val[i]=1; lru[i]=++clk; return i; }
  void set_bit(uint64_t block){ int i=touch(block>>6); bits[i]|=(uint64_t{1}<<(block&63)); }
  void clear_bit(uint64_t block){ int i=find(block>>6); if(i>=0) bits[i]&=~(uint64_t{1}<<(block&63)); } // line evicted
  bool probe(uint64_t block)const{ int i=find(block>>6); return i>=0 && ((bits[i]>>(block&63))&1); }
  uint64_t getbits(uint64_t pg)const{ int i=find(pg); return i>=0?bits[i]:0; }   // whole-region footprint
};

// BOUNDED region table for the data engine: set-assoc LRU holding {amap access-history, prev accessing PC}.
// Eviction = a page's learned access-map is LOST (contention with data-heavy pages) -- models the real
// SPPAM region table under data pressure, instead of an unbounded per-page map.
struct RegionTable {
  struct R{ uint64_t tag=0, amap=0, prev_ip=0; bool v=false; uint32_t lru=0; };
  int sets,ways; std::vector<R> t; uint32_t clk=0; uint64_t evicts=0;
  RegionTable(int s,int w):sets(s),ways(w),t((std::size_t)s*w){}
  R& get(uint64_t pg){ std::size_t base=(std::size_t)(pg%sets)*ways; int hit=-1,lw=0;
    for(int k=0;k<ways;k++){ if(t[base+k].v&&t[base+k].tag==pg)hit=k; if(!t[base+k].v||t[base+k].lru<t[base+lw].lru)lw=k; }
    if(hit>=0){ t[base+hit].lru=++clk; return t[base+hit]; }
    R&e=t[base+lw]; if(e.v)++evicts; e=R{}; e.tag=pg; e.v=true; e.lru=++clk; return e; }
  R* findp(uint64_t pg){ std::size_t base=(std::size_t)(pg%sets)*ways;
    for(int k=0;k<ways;k++) if(t[base+k].v&&t[base+k].tag==pg) return &t[base+k]; return nullptr; }
  void clearbit(uint64_t block){ R* r=findp(block>>6); if(r) r->amap &= ~(uint64_t{1}<<(block&63)); } // line evicted -> amap forgets it
  double kib()const{ return (double)sets*ways*(24/*tag*/+64/*amap*/+24/*prevpc*/)/8.0/1024.0; }
};

// IDEA 1: load-PC -> next-instruction-miss correlation. Instruction blocks are often L1I-resident
// (so they never miss to L2 and the branch graph is blind to their control flow), but the loads/
// stores executing in that resident code DO miss to L2 and carry their PC. We merge the l1d demand
// stream (load PCs) with the l1i miss stream by cycle, learn edge_lp[loadPC-block] -> delta to the
// next instruction miss (both are virtual code blocks; a tiny xlate learned from the instr stream
// turns the predicted code block physical), and measure how much instruction-miss coverage this
// adds ON TOP of the branch graph -- i.e., how much of the branch_cold tail the load PCs recover.
static void eval_instr_correlate(const std::string& l1i_path,const std::string& l1d_path,uint32_t W,uint64_t maxrec){
  struct Ev{ uint64_t cyc,a,p; uint8_t kind; };  // kind 0=instr miss, 1=load
  std::vector<Ev> ev; ev.reserve(1u<<22);
  { trace_reader tr(l1i_path); access_record r; uint64_t n=0;
    while(tr.next(r)){ if((atype)r.type==atype::PREFETCH)continue; if((r.ip>>6)!=(r.vaddr>>6))continue;
      ev.push_back({r.cycle,r.ip>>6,r.paddr>>6,0}); if(maxrec&&++n>=maxrec)break; } }
  { trace_reader tr(l1d_path); access_record r; uint64_t n=0;
    while(tr.next(r)){ auto t=(atype)r.type;
      if(t==atype::PREFETCH){ ev.push_back({r.cycle,0,r.paddr>>6,2}); if(maxrec&&++n>=maxrec)break; continue; } // berti data prefetch fill
      if(t!=atype::LOAD&&t!=atype::RFO)continue; if((r.ip>>6)==(r.vaddr>>6))continue;
      ev.push_back({r.cycle,r.ip>>6,r.paddr>>6,1}); if(maxrec&&++n>=maxrec)break; } } // a=load code block, p=DATA block
  std::stable_sort(ev.begin(),ev.end(),[](const Ev&a,const Ev&b){return a.cyc<b.cyc;});
  std::vector<uint64_t> ipb; for(auto&e:ev) if(e.kind==0) ipb.push_back(e.p);
  const uint32_t M=(uint32_t)ipb.size(); const std::size_t L=ev.size()-M;
  if(!M){ std::printf("correlate: no instruction misses in %s\n",l1i_path.c_str()); return; }
  std::unordered_map<uint64_t,std::vector<uint32_t>> idx; idx.reserve(M);
  for(uint32_t j=0;j<M;++j) idx[ipb[j]].push_back(j);
  auto hit_soon=[&](uint64_t b,uint32_t k)->int{ auto it=idx.find(b); if(it==idx.end())return -1;
    const auto&v=it->second; auto p=std::upper_bound(v.begin(),v.end(),k); return (p!=v.end()&&*p<=k+W)?(int)*p:-1; };
  std::printf("correlate %s: instr_misses=%u loads=%zu\n", l1i_path.substr(l1i_path.rfind('/')+1).c_str(),M,L);
  // Realizable merged-CF instruction predictor: bounded SPP-style edge table (counts = path-correctness
  // confidence, no usefulness tracking) + confidence-gated depth + a residency/prefetch map to drop
  // resident fetches. The prefetch map is SHARED with data (marked by load DATA blocks too, modeling the
  // physically-shared L2) -- so it costs the instruction predictor NO extra state (edge+xlate only).
  // denominator for COVERAGE: total upstream L2 accesses (instr demands + loads + berti pf) that miss in
  // the baseline (no instruction prefetcher). Computed once -- independent of the predictor config.
  uint64_t base_umiss=0, base_imiss=0;   // baseline (no instr prefetcher) upstream misses AND instruction-only misses
  { L2Cache bc(2048,16); for(const Ev& e : ev){ if(!bc.resident(e.p)){ ++base_umiss; if(e.kind==0)++base_imiss; } bc.install(e.p,false); } }

  { // MEASURE basic-block run length = consecutive +1 code-block transitions (BB spanning cache lines)
    uint64_t h[17]={0}, prev=0; int run=1; bool hp=false;
    for(const Ev& e : ev){ if(e.kind==2)continue; uint64_t b=e.a;
      if(!hp){ prev=b; hp=true; continue; } if(b==prev)continue;
      if((int64_t)b-(int64_t)prev==1) ++run; else { ++h[run>16?16:run]; run=1; } prev=b; }
    ++h[run>16?16:run]; uint64_t tot=0; double mean=0; for(int i=1;i<=16;i++){ tot+=h[i]; mean+=(double)i*h[i]; } mean/=tot?tot:1;
    auto p=[&](uint64_t x){ return tot?100.0*x/tot:0.0; };
    std::printf("  BB run-length: 1=%.0f%% 2=%.0f%% 3=%.0f%% 4=%.0f%% 5-8=%.0f%% >8=%.0f%% mean=%.2f blocks\n",
      p(h[1]),p(h[2]),p(h[3]),p(h[4]),p(h[5]+h[6]+h[7]+h[8]),p(h[9]+h[10]+h[11]+h[12]+h[13]+h[14]+h[15]+h[16]),mean); }

  auto run=[&](const char* nm,int Ne,int db,int Nx,int Nf,double conf,bool shared,bool use_filter,int depth,int ways,bool use_spatial,bool use_spp,bool filter_ft,bool use_run,bool use_bp){
    CFEdges G(Ne,db); IXlate xl(Nx); L2Cache cache(2048,16); RegionMap rmap(Nf,12);
    RegionMap amap(Nf,12);   // BOUNDED region ACCESS-map (code-page footprint) -- same struct/size as SPPAM's region table
    // idealized SPP: signature (rolling hash of recent instruction-block JUMP deltas) -> next delta.
    std::unordered_map<uint64_t,std::unordered_map<int64_t,uint32_t>> SPP; uint64_t sig=0;
    auto spp_pred=[&](uint64_t s)->std::pair<bool,int64_t>{ auto it=SPP.find(s); if(it==SPP.end())return{false,0};
      int64_t bd=0; uint32_t bc=0; for(auto&kv:it->second) if(kv.second>bc){bc=kv.second;bd=kv.first;} return {bc>=2,bd}; };
    // gshare branch predictor (4096 2-bit counters = 1 KiB, 12-bit global history): at a branchy edge, pick
    // d1(minority) vs d0(dominant) by history -> the deep walk follows the PREDICTED path, not blindly d0.
    std::vector<uint8_t> bp; if(use_bp) bp.assign(4096,1); uint32_t ghist=0,spec_ghist=0;
    auto bp_idx=[&](uint64_t b,uint32_t h){ return (uint32_t)((Hh(b)^h)&4095u); };
    auto do_fill=[&](uint64_t block,bool is_pf){ uint64_t ev=cache.install(block,is_pf); if(ev!=L2Cache::NONE) rmap.clear_bit(ev); rmap.set_bit(block); };
    uint64_t covered=0,iss=0,filt=0,filled=0,M0=0; uint64_t prev=0; bool hp=false;
    uint64_t last_spage=~uint64_t{0};   // spatial: only re-fetch the footprint on PAGE ENTRY (JIT refetch)
    uint64_t cur_run_src=0; int cur_run_len=1; bool have_run=false;   // BB run tracking
    auto issue_phys=[&](uint64_t phys){ if(use_filter && rmap.probe(phys)){ ++filt; return; } ++iss; if(!cache.resident(phys)){ ++filled; do_fill(phys,true); } };
    std::unordered_set<uint64_t> seen; uint64_t dmiss=0,t_cold=0,t_unt=0,t_cedge=0,t_wrong=0;
    for(const Ev& e : ev){
      const uint64_t block=e.a; const bool is_miss=(e.kind==0);
      if(is_miss){ ++M0;                              // instruction DEMAND at physical e.p
        if(cache.resident(e.p)){ if(cache.demand_useful(e.p)) ++covered; } // hit; covered iff hit an unused prefetch
        else { ++dmiss;                               // UNCOVERED MISS -> classify why (depth-1 predecessor)
          if(!seen.count(block)) ++t_cold;            // first sighting -> unavoidable
          else if(hp){ int64_t delta=(int64_t)block-(int64_t)prev; auto* ed=G.get(prev);
            int64_t pred = (ed&&ed->c0)? ed->d0 : 1;  // next-line if the edge is cold
            if(pred==delta) ++t_unt;                  // predicted (=> prefetched) but evicted before use = untimely
            else if(!ed||!ed->c0) ++t_cedge;          // edge cold, block != next-line -> transition not learned
            else ++t_wrong; }                         // edge predicted a DIFFERENT successor
          else ++t_cedge;
          do_fill(e.p,false); }                       // real miss -> demand fill
        // SPATIAL predictor: on PAGE ENTRY, prefetch this code page's learned footprint (JIT re-fetch of the
        // whole hot-code region right when execution returns to it -- recovers the capacity/timeliness residual).
        if(use_spatial){ uint64_t pg=e.p>>6;
          if(pg!=last_spage){ uint64_t m=amap.getbits(pg);
            for(int o=0;o<64;++o) if((m>>o)&1) issue_phys((pg<<6)|(uint64_t)o); }
          amap.set_bit(e.p); last_spage=pg; }
        seen.insert(block);
      } else if(shared){ do_fill(e.p,false); }        // load / berti data fills the shared L2
      if(e.kind==2) continue;                         // data prefetch: fills L2 only, no code prediction
      if(!hp || block!=prev){
        // SPP: train sig(at prev) -> observed delta, then roll the signature (fallthroughs optionally filtered).
        if(hp && use_spp){ int64_t delta=(int64_t)block-(int64_t)prev;
          if(!filter_ft || delta!=1){ ++SPP[sig][delta]; sig=((sig<<10)|(((uint64_t)(delta+512))&0x3FF))&0xFFFFFFFFFFull; } }
        uint64_t cur=block; spec_ghist=ghist;         // deep walk starts from the current global history
        for(int d=0; d<depth; ++d){
          auto* ed=G.get(cur); int64_t d0=1; double share=0.0;
          if(ed && ed->c0){ double t=ed->c0+ed->c1; share=ed->c0/t;
            if(use_bp && ed->c1>0){ bool td1 = bp[bp_idx(cur,spec_ghist)]>=2;  // gshare picks the path at a branch
              d0 = td1?ed->d1:ed->d0; spec_ghist=((spec_ghist<<1)|(td1?1u:0u))&0xFFFu; }
            else d0=ed->d0; }
          else if(use_spp){ auto sp=spp_pred(sig); if(sp.first) d0=sp.second; } // COLD edge -> SPP delta (else next-line)
          if(d>0 && !use_bp && share<conf) break;     // confidence-gated depth (BP walk goes deep, follows path)
          uint64_t nx=(uint64_t)((int64_t)cur+d0); uint32_t pp;
          if(xl.get(nx>>6,pp)) issue_phys(((uint64_t)pp<<6)|(nx&63));          // dominant successor = BB start
          if(use_run && ed){ for(int r=1;r<ed->run;++r){ uint64_t rb=nx+(uint64_t)r; uint32_t rp;  // fetch the rest of the BB
            if(xl.get(rb>>6,rp)) issue_phys(((uint64_t)rp<<6)|(rb&63)); } }
          if(ways>=2 && ed && ed->c1>0){ uint64_t n1=(uint64_t)((int64_t)cur+ed->d1); uint32_t p1;
            if(xl.get(n1>>6,p1)) issue_phys(((uint64_t)p1<<6)|(n1&63)); }      // 2-wide: 2nd (minority) successor
          cur=nx;
        }
        if(hp){
          if(use_bp){ auto* ep=G.get(prev); if(ep && ep->c0 && ep->c1){ int64_t d=(int64_t)block-(int64_t)prev; // train gshare on real branch outcomes
            int out=(d==ep->d1)?1:((d==ep->d0)?0:-1);
            if(out>=0){ uint32_t i=bp_idx(prev,ghist); if(out){ if(bp[i]<3)++bp[i]; } else { if(bp[i]>0)--bp[i]; } ghist=((ghist<<1)|(uint32_t)out)&0xFFFu; } } }
          G.obs(prev,(int64_t)block-(int64_t)prev);
          if(use_run){ int64_t d=(int64_t)block-(int64_t)prev;   // BB run: extend on +1, finalize edge run on a jump
            if(d==1) ++cur_run_len;
            else { if(have_run) G.set_run(cur_run_src,cur_run_len); cur_run_src=prev; cur_run_len=1; have_run=true; } } }
        prev=block; hp=true;
      }
      if(is_miss) xl.ins(block>>6,(uint32_t)(e.p>>6));
    }
    auto pc=[&](uint64_t x){ return dmiss?100.0*x/dmiss:0.0; };
    std::printf("  %-16s cov_i=%.1f%% use=%.1f%% fill/dem=%.2f %4.1fKiB | uncov: cold=%.0f%% untimely=%.0f%% cold_edge=%.0f%% wrong=%.0f%%\n",
      nm,base_imiss?100.0*((double)base_imiss-(double)dmiss)/(double)base_imiss:0.0, filled?100.0*covered/filled:0.0, (double)filled/M0, G.kib()+xl.kib(),
      pc(t_cold),pc(t_unt),pc(t_cedge),pc(t_wrong));
  };
  std::printf("  (coverage denominator = %llu baseline upstream misses; instr demands = %u)\n",
    (unsigned long long)base_umiss, M);
  //   name          Ne  db Nx Nf conf shr flt dep way spat spp fFT  run  bp -- INSTRUCTION DESIGN: does any
  //   add-on beat the buildable branch-graph baseline (256e/8b edge table + next-line + xlate + filter)?
  run("BG d1 (build)",  256, 8,32, 64,0.6,true,true, 1,1,false,false,false,false,false); // <- the buildable design
  run("BG+spatial",     256, 8,32, 64,0.6,true,true, 1,1,true ,false,false,false,false); // + per-page footprint (UNBOUNDED = optimistic)
  run("BG+spp",         256, 8,32, 64,0.6,true,true, 1,1,false,true ,true ,false,false); // + SPP cold-edge fallback (filter fallthrough)
  run("BG+bbrun",       256, 8,32, 64,0.6,true,true, 1,1,false,false,false,true ,false); // + fetch rest-of-BB per edge
  run("BG+spat rgn256", 256, 8,32,256,0.6,true,true, 1,1,true ,false,false,false,false); // + bigger region table (256x12=3072)
  run("BG+spat rgn1K",  256, 8,32,1024,0.6,true,true,1,1,true ,false,false,false,false); // + 1024x12 region table
  run("BG d2 gated",    256, 8,32, 64,0.6,true,true, 2,1,false,false,false,false,false); // depth-2, confidence-gated
  run("BG 2-wide",      256, 8,32, 64,0.6,true,true, 1,2,false,false,false,false,false); // issue minority successor too

  // ---- STRUCTURE of the UNCOVERED misses: predictable (temporal/page-local/stride) or novel? ----
  {
    CFEdges G(256,8); IXlate xl(256); L2Cache cache(2048,16); RegionMap rmap(64,12);
    auto do_fill=[&](uint64_t b,bool pf){ uint64_t x=cache.install(b,pf); if(x!=L2Cache::NONE)rmap.clear_bit(x); rmap.set_bit(b); };
    std::vector<uint64_t> bd; std::vector<uint8_t> cov; bd.reserve(M); cov.reserve(M);
    uint64_t prev=0; bool hp=false;
    for(const Ev& e : ev){ const uint64_t block=e.a; const bool is_miss=(e.kind==0);
      if(is_miss){ bool res=cache.resident(e.p); bool c=false;
        if(res) c=cache.demand_useful(e.p); else do_fill(e.p,false);
        bd.push_back(block); cov.push_back(res?(c?2:1):0); } // 0=true miss, 1=temporal hit, 2=prefetch-covered
      else { do_fill(e.p,false); if(e.kind==2) continue; }
      if(!hp||block!=prev){ auto* ed=G.get(block); int64_t d0=(ed&&ed->c0)?ed->d0:1;
        uint64_t nx=(uint64_t)((int64_t)block+d0); uint32_t pp;
        if(xl.get(nx>>6,pp)){ uint64_t ph=((uint64_t)pp<<6)|(nx&63); if(!rmap.probe(ph)&&!cache.resident(ph)) do_fill(ph,true); }
        if(hp) G.obs(prev,(int64_t)block-(int64_t)prev); prev=block; hp=true; }
      if(is_miss) xl.ins(block>>6,(uint32_t)(e.p>>6));
    }
    const uint32_t N=(uint32_t)bd.size();
    uint64_t nmiss=0,nhit=0,ncov=0; for(uint32_t i=0;i<N;++i){ if(cov[i]==0)++nmiss; else if(cov[i]==1)++nhit; else ++ncov; }
    std::printf("  === DEFINITIVE COVERAGE (merged CF, 1.4KiB, depth-1) ===\n");
    std::printf("  of %u instr demands: prefetch-covered=%.0f%% already-resident-hit=%.0f%% true-miss=%.0f%%\n",
      N,100.0*ncov/N,100.0*nhit/N,100.0*nmiss/N);
    std::printf("  baseline instr misses (no instr-pf)=%llu ; with-pf true misses=%llu\n",(unsigned long long)base_imiss,(unsigned long long)nmiss);
    std::printf("  COVERAGE of instruction baseline misses = %.1f%%   (misses removed / baseline instr misses)\n",
      base_imiss? 100.0*(base_imiss-nmiss)/base_imiss : 0.0);
    std::printf("  [same numerator over TOTAL upstream misses incl data = %.1f%%]\n", base_umiss?100.0*(base_imiss-nmiss)/base_umiss:0.0);
    std::unordered_map<uint64_t,uint32_t> lastblk,lastpg; std::unordered_map<int64_t,uint64_t> stride;
    uint64_t unc=0,temporal=0,pagelocal=0,novelpage=0,rdn=0,rdm=0,rdf=0;
    for(uint32_t i=0;i<N;++i){ uint64_t b=bd[i],pg=b>>6;
      if(cov[i]==0){ ++unc;                              // TRUE MISS only (not temporal hits)
        auto bi=lastblk.find(b); auto pi=lastpg.find(pg);
        if(bi!=lastblk.end()) ++temporal;              // exact block seen before -> temporal/footprint
        else if(pi!=lastpg.end()) ++pagelocal;         // page seen, new block -> spatial footprint
        else ++novelpage;                              // new page -> cold
        if(pi!=lastpg.end()){ uint32_t rd=i-pi->second; if(rd<=64)++rdn; else if(rd<=4096)++rdm; else ++rdf; }
        if(i>0) ++stride[(int64_t)b-(int64_t)bd[i-1]];
      }
      lastblk[b]=i; lastpg[pg]=i;
    }
    // EVICTION TEST: same predictor, but data does NOT fill the L2 (instructions protected). If the true-miss
    // count collapses, the ~80% ceiling is EVICTION (data knocking out predictable instr lines), not prediction.
    uint64_t tmiss=0;
    { CFEdges G2(256,8); IXlate xl2(256); L2Cache c2(2048,16); RegionMap r2(64,12);
      auto f2=[&](uint64_t b,bool pf){ uint64_t x=c2.install(b,pf); if(x!=L2Cache::NONE)r2.clear_bit(x); r2.set_bit(b); };
      uint64_t p2=0; bool h2=false;
      for(const Ev& e : ev){ const uint64_t block=e.a; const bool is_miss=(e.kind==0);
        if(is_miss){ if(c2.resident(e.p)) c2.demand_useful(e.p); else { ++tmiss; f2(e.p,false); } }
        if(e.kind!=0) continue;                        // DATA does NOT fill L2 -> instructions protected
        if(!h2||block!=p2){ auto* ed=G2.get(block); int64_t d0=(ed&&ed->c0)?ed->d0:1;
          uint64_t nx=(uint64_t)((int64_t)block+d0); uint32_t pp;
          if(xl2.get(nx>>6,pp)){ uint64_t ph=((uint64_t)pp<<6)|(nx&63); if(!r2.probe(ph)&&!c2.resident(ph)) f2(ph,true); }
          if(h2)G2.obs(p2,(int64_t)block-(int64_t)p2); p2=block; h2=true; }
        xl2.ins(block>>6,(uint32_t)(e.p>>6)); }
      std::printf("  EVICTION TEST (instr-only L2, no data eviction): true-miss %llu -> instr coverage %.1f%%  (vs shared %.1f%%)\n",
        (unsigned long long)tmiss, base_imiss?100.0*(base_imiss-tmiss)/base_imiss:0.0, base_imiss?100.0*(base_imiss-nmiss)/base_imiss:0.0); }
    // KEEP-ALIVE: stochastic residency-filter bypass. When the predicted-path walk lands on a RESIDENT block,
    // with prob p re-issue the prefetch = MRU-touch it (keep it alive vs data eviction). Fill stays DEPTH-1
    // (isolates keep-alive from prefetch depth); the touch walk extends to Kd. Cost = extra L2-hitting
    // prefetches (no DRAM traffic). Does cheap keep-alive recover the ~13pt idealized eviction gap?
    { auto keepalive=[&](double p,int Kd){ CFEdges G(256,8); IXlate xl(256); L2Cache cache(2048,16); RegionMap rmap(64,12);
        auto do_fill=[&](uint64_t b,bool pf){ uint64_t x=cache.install(b,pf); if(x!=L2Cache::NONE)rmap.clear_bit(x); rmap.set_bit(b); };
        uint64_t prev=0; bool hp=false; uint64_t nm2=0,ka=0,idem=0,rng=0x243F6A8885A308D3ull;
        auto rndlt=[&](double pr){ rng+=0x9E3779B97F4A7C15ull; uint64_t z=rng; z=(z^(z>>30))*0xBF58476D1CE4E5B9ull; z=(z^(z>>27))*0x94D049BB133111EBull; z^=z>>31; return (double)(z>>11)*(1.0/9007199254740992.0)<pr; };
        for(const Ev& e : ev){ const uint64_t block=e.a; const bool is_miss=(e.kind==0);
          if(is_miss){ ++idem; if(cache.resident(e.p)) cache.demand_useful(e.p); else { ++nm2; do_fill(e.p,false); } }
          else { do_fill(e.p,false); if(e.kind==2) continue; }
          if(!hp||block!=prev){ uint64_t cur=block;
            for(int d=0; d<Kd; ++d){ auto* ed=G.get(cur); int64_t d0=(ed&&ed->c0)?ed->d0:1;
              uint64_t nx=(uint64_t)((int64_t)cur+d0); uint32_t pp;
              if(xl.get(nx>>6,pp)){ uint64_t ph=((uint64_t)pp<<6)|(nx&63);
                if(cache.resident(ph)){ if(p>0.0&&rndlt(p)){ cache.touch(ph); ++ka; } }   // KEEP-ALIVE touch
                else if(d==0 && !rmap.probe(ph)) do_fill(ph,true); }                        // fill only at depth 0
              cur=nx; }
            if(hp) G.obs(prev,(int64_t)block-(int64_t)prev); prev=block; hp=true; }
          if(is_miss) xl.ins(block>>6,(uint32_t)(e.p>>6)); }
        return std::make_pair(base_imiss?100.0*(base_imiss-nm2)/base_imiss:0.0, idem?100.0*ka/idem:0.0); };
      std::printf("  --- KEEP-ALIVE (stochastic filter bypass) : shared floor=%.1f%%  protected ceiling=%.1f%% ---\n",
        base_imiss?100.0*(base_imiss-nmiss)/base_imiss:0.0, base_imiss?100.0*(base_imiss-tmiss)/base_imiss:0.0);
      for(int Kd : {1,16}) for(double p : {1.0}){ auto r=keepalive(p,Kd);
        std::printf("    Kd=%-2d p=%.3f : coverage=%.1f%%  keep-alive-touches=%.1f%% of instr demands (demand-coupled: near-useless)\n",Kd,p,r.first,r.second); } }
    // MECHANISM B: eviction-triggered refetch of HOT instr lines. Bounded recency table tracks each instr
    // block's last use-time; when the cache evicts an instr line used within the last R events (recurring/hot),
    // refetch it (a real DRAM prefetch -> counted as traffic). Fires AT the eviction => works cross-phase
    // (during data storms) with no demand coupling. One-level refetch (its own victim is not re-refetched).
    { auto mechB=[&](uint32_t R)->std::pair<double,double>{
        CFEdges G(256,8); IXlate xl(256); L2Cache cache(2048,16); RegionMap rmap(64,12);
        const uint32_t RB=1u<<16; std::vector<uint64_t> rtag(RB,~uint64_t{0}); std::vector<uint32_t> rlast(RB,0);
        auto ridx=[&](uint64_t pb){ return (uint32_t)((pb*0x9E3779B97F4A7C15ull>>24)&(RB-1)); };
        auto rec_touch=[&](uint64_t pb,uint32_t now){ uint32_t i=ridx(pb); rtag[i]=pb; rlast[i]=now; };
        auto rec_hot=[&](uint64_t pb,uint32_t now){ uint32_t i=ridx(pb); return rtag[i]==pb && (now-rlast[i])<R; };
        uint64_t nm=0,refetch=0,idem=0; uint32_t now=0;
        auto do_fill=[&](uint64_t b,bool pf,bool instr){ bool vi=false; uint64_t x=cache.install(b,pf,instr,&vi);
          if(x!=L2Cache::NONE){ rmap.clear_bit(x);
            if(vi && rec_hot(x,now)){ ++refetch; bool v2=false; uint64_t x2=cache.install(x,true,true,&v2);
              if(x2!=L2Cache::NONE) rmap.clear_bit(x2); rmap.set_bit(x); } }
          rmap.set_bit(b); };
        uint64_t prev=0; bool hp=false;
        for(const Ev& e : ev){ ++now; const uint64_t block=e.a; const bool is_miss=(e.kind==0);
          if(is_miss){ ++idem; rec_touch(e.p,now); if(cache.resident(e.p)) cache.demand_useful(e.p); else { ++nm; do_fill(e.p,false,true); } }
          else { do_fill(e.p,false,false); if(e.kind==2) continue; }
          if(!hp||block!=prev){ auto* ed=G.get(block); int64_t d0=(ed&&ed->c0)?ed->d0:1;
            uint64_t nx=(uint64_t)((int64_t)block+d0); uint32_t pp;
            if(xl.get(nx>>6,pp)){ uint64_t ph=((uint64_t)pp<<6)|(nx&63); if(!rmap.probe(ph)&&!cache.resident(ph)) do_fill(ph,true,true); }
            if(hp) G.obs(prev,(int64_t)block-(int64_t)prev); prev=block; hp=true; }
          if(is_miss) xl.ins(block>>6,(uint32_t)(e.p>>6)); }
        return std::make_pair(base_imiss?100.0*(base_imiss-nm)/base_imiss:0.0, idem?100.0*refetch/idem:0.0); };
      std::printf("  --- MECHANISM B: eviction-triggered refetch of hot instr lines (DRAM traffic) : floor=%.1f%% ceiling=%.1f%% ---\n",
        base_imiss?100.0*(base_imiss-nmiss)/base_imiss:0.0, base_imiss?100.0*(base_imiss-tmiss)/base_imiss:0.0);
      for(uint32_t R : {16384u,65536u,0xFFFFFFFFu}){ auto r=mechB(R);
        std::printf("    R=%-10u : coverage=%.1f%%  refetch-traffic=%.1f%% of instr demands\n",(unsigned)R,r.first,r.second); } }
    // MECHANISM A: spaced adaptive keep-alive queue. Sample 1/N of predicted-RESIDENT hits into a bounded
    // ring; drain one entry every M events by MRU-touching it (L2 hit, NO DRAM) and re-enqueuing it, so a
    // rotating hot set is refreshed evenly across time (incl. data storms). Adaptive N: ring full -> N*=2
    // (sample less); ring empty on drain -> N/=2 (sample more). Self-paces to bandwidth; holds long-reuse
    // instr lines alive through the idle gap that B correctly-but-uselessly evicts.
    { auto mechA=[&](uint32_t QCAP,uint32_t M)->std::pair<double,double>{
        CFEdges G(256,8); IXlate xl(256); L2Cache cache(2048,16); RegionMap rmap(64,12);
        auto do_fill=[&](uint64_t b,bool pf){ uint64_t x=cache.install(b,pf); if(x!=L2Cache::NONE)rmap.clear_bit(x); rmap.set_bit(b); };
        std::vector<uint64_t> ring(QCAP+1,0); uint32_t qh=0,qt=0; const uint32_t CAP=QCAP+1;
        auto qfull=[&]{ return (qt+1)%CAP==qh; }; auto qempty=[&]{ return qh==qt; };
        uint32_t N=8; uint64_t sampctr=0,touches=0,evt=0,nm=0,idem=0; uint64_t prev=0; bool hp=false;
        for(const Ev& e : ev){ const uint64_t block=e.a; const bool is_miss=(e.kind==0);
          if(is_miss){ ++idem; if(cache.resident(e.p)) cache.demand_useful(e.p); else { ++nm; do_fill(e.p,false); } }
          else { do_fill(e.p,false); }
          if(e.kind!=2 && (!hp||block!=prev)){ auto* ed=G.get(block); int64_t d0=(ed&&ed->c0)?ed->d0:1;
            uint64_t nx=(uint64_t)((int64_t)block+d0); uint32_t pp;
            if(xl.get(nx>>6,pp)){ uint64_t ph=((uint64_t)pp<<6)|(nx&63);
              if(cache.resident(ph)){ if(++sampctr>=N){ sampctr=0; if(!qfull()){ ring[qt]=ph; qt=(qt+1)%CAP; } else N<<=1; } }
              else if(!rmap.probe(ph)) do_fill(ph,true); }
            if(hp) G.obs(prev,(int64_t)block-(int64_t)prev); prev=block; hp=true; }
          if(is_miss) xl.ins(block>>6,(uint32_t)(e.p>>6));
          if(++evt>=M){ evt=0; if(!qempty()){ uint64_t b=ring[qh]; qh=(qh+1)%CAP;
              if(cache.resident(b)){ cache.touch(b); ++touches; if(!qfull()){ ring[qt]=b; qt=(qt+1)%CAP; } } }
            else if(N>1) N>>=1; } }
        return std::make_pair(base_imiss?100.0*(base_imiss-nm)/base_imiss:0.0, idem?100.0*touches/idem:0.0); };
      std::printf("  --- MECHANISM A: spaced adaptive keep-alive queue (L2-hit traffic) ---\n");
      for(uint32_t QCAP : {2048u}) for(uint32_t M : {8u,64u}){ auto r=mechA(QCAP,M);
        std::printf("    QCAP=%-5u M=%-4u : coverage=%.1f%%  keep-alive-touches=%.1f%% of instr demands (blind to idle set)\n",(unsigned)QCAP,(unsigned)M,r.first,r.second); } }
    // LEVER: hard way-partition. Reserve kI of 16 ways per set for instructions (data uses 16-kI). The dying
    // lines are idle-long-reuse -> invisible to any keep-alive predictor during their idle, so the only thing
    // that reaches the ceiling is RESERVING capacity to hold them. Quantifies that capacity (data cost implicit
    // -- data loses 16-kI ways -- and shows in a real sim, not here).
    { auto part=[&](int kI)->double{ CFEdges G(256,8); IXlate xl(256);
        L2Cache ic(2048,kI), dc(2048,16-kI>0?16-kI:1); RegionMap rmap(64,12);
        auto ifill=[&](uint64_t b,bool pf){ uint64_t x=ic.install(b,pf); if(x!=L2Cache::NONE)rmap.clear_bit(x); rmap.set_bit(b); };
        uint64_t prev=0; bool hp=false; uint64_t nm=0;
        for(const Ev& e : ev){ const uint64_t block=e.a; const bool is_miss=(e.kind==0);
          if(is_miss){ if(ic.resident(e.p)) ic.demand_useful(e.p); else { ++nm; ifill(e.p,false); } }
          else { dc.install(e.p,false); if(e.kind==2) continue; }
          if(!hp||block!=prev){ auto* ed=G.get(block); int64_t d0=(ed&&ed->c0)?ed->d0:1;
            uint64_t nx=(uint64_t)((int64_t)block+d0); uint32_t pp;
            if(xl.get(nx>>6,pp)){ uint64_t ph=((uint64_t)pp<<6)|(nx&63); if(!rmap.probe(ph)&&!ic.resident(ph)) ifill(ph,true); }
            if(hp) G.obs(prev,(int64_t)block-(int64_t)prev); prev=block; hp=true; }
          if(is_miss) xl.ins(block>>6,(uint32_t)(e.p>>6)); }
        return base_imiss?100.0*((double)base_imiss-(double)nm)/(double)base_imiss:0.0; };  // signed: partition can miss MORE than baseline
      std::printf("  --- LEVER: hard way-partition, kI instr ways of 16 (data gets 16-kI) : floor=%.1f%% ceiling=%.1f%% ---\n",
        base_imiss?100.0*(base_imiss-nmiss)/base_imiss:0.0, base_imiss?100.0*(base_imiss-tmiss)/base_imiss:0.0);
      for(int kI : {1,2,4,8}) std::printf("    kI=%-2d instr ways (%5d lines) : coverage=%.1f%%\n",kI,kI*2048,part(kI)); }
    std::vector<std::pair<int64_t,uint64_t>> sv(stride.begin(),stride.end());
    std::sort(sv.begin(),sv.end(),[](auto&a,auto&b){return a.second>b.second;});
    auto pc=[&](uint64_t x){ return unc?100.0*x/unc:0.0; };
    std::printf("  UNCOVERED=%llu | temporal(block-recur)=%.0f%% page-local(new blk)=%.0f%% novel-page=%.0f%%\n",
      (unsigned long long)unc,pc(temporal),pc(pagelocal),pc(novelpage));
    std::printf("    page reuse-dist: <=64=%.0f%% <=4K=%.0f%% far=%.0f%%  | top strides:", pc(rdn),pc(rdm),pc(rdf));
    for(int k=0;k<6&&k<(int)sv.size();k++) std::printf(" %+lld=%.0f%%",(long long)sv[k].first,pc(sv[k].second));
    std::printf("\n");
  }
}

// ---- instruction-stream prefetcher DSE: drives the REAL iprefetch_predictor (bounded edge table
// + xlate + residency filter, the shipping model) with the 2-wide / order-2 additions layered on
// top via params. Coverage = fraction of instruction L2 demands whose physical block a prefetch
// anticipated within W; state = the predictor's actual bounded budget.
static void eval_l1i(const std::string& trace,uint32_t W,uint64_t maxrec,int /*depth*/,int /*bg_log2*/){
  std::vector<uint64_t> ib, pb;
  { trace_reader tr(trace); access_record r;
    while(tr.next(r)){ auto t=(atype)r.type; if(t==atype::PREFETCH)continue;
      if((r.ip>>6)!=(r.vaddr>>6))continue; ib.push_back(r.ip>>6); pb.push_back(r.paddr>>6);
      if(maxrec&&ib.size()>=maxrec)break; } }
  const uint32_t N=(uint32_t)ib.size();
  if(!N){ std::printf("l1i: no instruction records in %s\n",trace.c_str()); return; }
  // CONSECUTIVE L1I-MISS delta distribution (pure miss stream, no load interleaving): are misses
  // sequential/near (predictable LRU turnover) or scattered? + length of +1 (sequential) miss runs.
  { uint64_t p1=0,pf=0,pb2=0,spg=0,xpg=0,tot=0,run=1,h[17]={0};
    for(uint32_t i=1;i<N;++i){ int64_t d=(int64_t)ib[i]-(int64_t)ib[i-1]; ++tot;
      if(d==1)++p1; else if(d>=2&&d<=4)++pf; else if(d<0&&d>=-4)++pb2;
      else if((ib[i]>>6)==(ib[i-1]>>6))++spg; else ++xpg;
      if(d==1)++run; else { ++h[run>16?16:run]; run=1; } }
    ++h[run>16?16:run]; auto P=[&](uint64_t x){ return tot?100.0*x/tot:0.0; };
    uint64_t rt=0; double rm=0; for(int i=1;i<=16;i++){ rt+=h[i]; rm+=(double)i*h[i]; } rm/=rt?rt:1;
    std::printf("l1i %s: %u misses | consecutive-delta: +1=%.0f%% +2..4=%.0f%% -1..-4=%.0f%% same-page-other=%.0f%% cross-page=%.0f%%\n",
      trace.substr(trace.rfind('/')+1).c_str(),N,P(p1),P(pf),P(pb2),P(spg),P(xpg));
    std::printf("       +1 sequential-run length: mean=%.2f  (1=%.0f%% 2=%.0f%% 3-4=%.0f%% 5-8=%.0f%% >8=%.0f%%)\n",
      rm, rt?100.0*h[1]/rt:0, rt?100.0*h[2]/rt:0, rt?100.0*(h[3]+h[4])/rt:0, rt?100.0*(h[5]+h[6]+h[7]+h[8])/rt:0,
      rt?100.0*(h[9]+h[10]+h[11]+h[12]+h[13]+h[14]+h[15]+h[16])/rt:0);
    // are the CROSS-PAGE transitions (the residual) predictable? recurrence of source->target edge + of the target block.
    { std::unordered_set<uint64_t> eseen,tseen; uint64_t xt=0,erec=0,trec=0;
      for(uint32_t i=1;i<N;++i){ if((ib[i]>>6)!=(ib[i-1]>>6)){ ++xt;
        uint64_t ek=Hh(ib[i-1])*0x9E3779B97F4A7C15ull ^ ib[i];
        if(eseen.count(ek))++erec; else eseen.insert(ek);
        if(tseen.count(ib[i]))++trec; else tseen.insert(ib[i]); } }
      auto q=[&](uint64_t x){ return xt?100.0*x/xt:0.0; };
      std::printf("       cross-page transitions=%llu: source->target edge recurs=%.0f%% | target block seen-before=%.0f%%\n",
        (unsigned long long)xt,q(erec),q(trec)); } }
  std::unordered_map<uint64_t,std::vector<uint32_t>> idx; idx.reserve(N);
  for(uint32_t i=0;i<N;++i) idx[pb[i]].push_back(i);
  auto hit_soon=[&](uint64_t b,uint32_t i)->int{ auto it=idx.find(b); if(it==idx.end())return -1;
    const auto&v=it->second; auto p=std::upper_bound(v.begin(),v.end(),i); return (p!=v.end()&&*p<=i+W)?(int)*p:-1; };
  { std::unordered_set<uint64_t> b,pg; std::unordered_map<uint64_t,uint32_t> ef; uint64_t pv=0; bool hv=false;
    for(uint32_t i=0;i<N;++i){ b.insert(ib[i]); pg.insert(ib[i]>>6); if(hv) ++ef[Hh(pv)^ib[i]]; pv=ib[i]; hv=true; }
    std::vector<uint32_t> f; f.reserve(ef.size()); uint64_t tot=0; for(auto&kv:ef){ f.push_back(kv.second); tot+=kv.second; }
    std::sort(f.begin(),f.end(),std::greater<uint32_t>());
    auto topk=[&](std::size_t k){ uint64_t s=0; for(std::size_t j=0;j<k&&j<f.size();++j)s+=f[j]; return tot?100.0*s/tot:0.0; };
    std::printf("l1i %s demands=%u | distinct blocks=%zu pages=%zu o1_edges=%zu | top-edge coverage: 256=%.0f%% 1k=%.0f%% 4k=%.0f%% 16k=%.0f%% 64k=%.0f%%\n",
      trace.substr(trace.rfind('/')+1).c_str(),N,b.size(),pg.size(),ef.size(),topk(256),topk(1024),topk(4096),topk(16384),topk(65536)); }
  auto run=[&](const char* name, params P){
    iprefetch_predictor ip(P);
    std::vector<uint8_t> covered(N,0); uint64_t issued=0,correct=0,cov=0; uint32_t ci=0;
    auto issue=[&](uint64_t pblk){ ++issued; int j=hit_soon(pblk,ci); if(j>=0){ ++correct; covered[j]=1; } };
    for(uint32_t i=0;i<N;++i){ ci=i; ip.operate(ib[i], pb[i], issue); }
    for(uint32_t i=0;i<N;++i) if(covered[i])++cov;
    std::printf("  %-26s cov=%.1f%% acc=%.1f%% pf/dem=%.2f state=%.2fKiB\n",
      name,100.0*cov/N,issued?100.0*correct/issued:0.0,(double)issued/N,P.instr_state_kib());
  };
  params base; base.instr_la_depth=12;
  run("real_base 256e o1 1wide", base);
  { params p=base; p.instr_issue_ways=2; run("+2wide (share>=25%)", p); }
  { params p=base; p.instr_issue_ways=2; p.instr_ctx_order=2; p.instr_table_entries=1024; run("+2wide+order2 1024e", p); }
  { params p=base; p.instr_issue_ways=2; p.instr_ctx_order=2; p.instr_table_entries=4096; run("+2wide+order2 4096e", p); }
  { params p=base; p.instr_issue_ways=1; p.instr_ctx_order=1; p.instr_table_entries=8192; p.instr_xlate_entries=1024; run("o1_1wide 8192e",p); }

  // ---- taxonomy of the FIXED base predictor's misses (256e, order-1, next-line fallback) ----
  {
    params P; P.instr_la_depth=12;
    iprefetch_predictor ip(P);
    std::vector<uint8_t> covered(N,0);
    std::unordered_map<uint64_t,uint32_t> last_seen; last_seen.reserve(N/4);
    uint32_t ci=0;
    auto issue=[&](uint64_t pblk){ int j=hit_soon(pblk,ci); if(j>=0) covered[j]=1; };
    long COLD=0,SEQ=0,BCOLD=0,BMIN=0,BWRONG=0,BOK=0,miss=0;
    uint64_t prevmiss=0; bool haveprev=false;
    for(uint32_t i=0;i<N;++i){
      ci=i;
      if(!covered[i]){
        ++miss;
        if(last_seen.find(ib[i])==last_seen.end()) ++COLD;       // first-ever sighting -> unavoidable
        else {
          auto sp=last_seen.find(ib[i]-1);                        // sequential predecessor block, seen recently?
          if(sp!=last_seen.end() && i-sp->second<=W) ++SEQ;       // next-line should have covered -> xlate/filter/timeliness
          else if(haveprev){                                     // branch target: examine the source edge (prior miss)
            int64_t d0,d1; int c0,c1; int64_t delta=(int64_t)ib[i]-(int64_t)prevmiss;
            if(!ip.query(0,prevmiss,false,d0,d1,c0,c1)) ++BCOLD;  // edge cold -> next-line fired the wrong way
            else if(d0==delta) ++BOK;                            // predicted right but missed (xlate/depth/timeliness)
            else if(c1>0 && d1==delta) ++BMIN;                   // actual == minority successor (2-wide catchable)
            else ++BWRONG;                                       // predicted a different / untracked target
          } else ++BCOLD;
        }
      }
      ip.operate(ib[i], pb[i], issue);
      last_seen[ib[i]]=i; prevmiss=ib[i]; haveprev=true;
    }
    auto pc=[&](long x){ return miss?100.0*x/miss:0.0; };
    std::printf("  MISS TAXONOMY (fixed base): misses=%ld (%.1f%% of demands) | cold_block=%.0f%% seq_miss=%.0f%% branch_cold=%.0f%% branch_minority=%.0f%% branch_wrong=%.0f%% branch_ok=%.0f%%\n",
      miss,100.0*miss/N,pc(COLD),pc(SEQ),pc(BCOLD),pc(BMIN),pc(BWRONG),pc(BOK));
  }

  // ---- IDEA 2 (per-code-page spatial access map) + IDEA 3 (order-4 temporal history) incremental
  //      over the branch graph. Do inlined-code spatial patterns or deeper history recover the tail?
  {
    params P; P.instr_la_depth=12; iprefetch_predictor bg(P);
    std::unordered_map<uint64_t,uint64_t> pmap, xl;    // code page -> access-map bits ; vpage->ppage
    std::unordered_map<uint64_t,LPEdge> temp;          // order-4 history hash -> next-ib delta
    std::vector<uint8_t> cbg(N,0), cs(N,0), ct(N,0);
    uint32_t ci=0; uint64_t hist=0, p1=0; bool hv=false; uint64_t s_iss=0,s_cor=0;
    auto issbg=[&](uint64_t pblk){ int j=hit_soon(pblk,ci); if(j>=0) cbg[j]=1; };
    for(uint32_t i=0;i<N;++i){ ci=i; uint64_t page=ib[i]>>6; int off=(int)(ib[i]&63);
      // IDEA 2: spatial -- prefetch the page's already-seen blocks ahead of the current offset.
      { auto it=pmap.find(page); auto xi=xl.find(page);
        if(it!=pmap.end() && xi!=xl.end()){ uint64_t m=it->second;
          for(int o=off+1;o<64;++o) if((m>>o)&1){ uint64_t phys=(xi->second<<6)|(uint64_t)o;
            ++s_iss; int j=hit_soon(phys,i); if(j>=0){ ++s_cor; cs[j]=1; } } } }
      // IDEA 3: temporal -- order-4 history hash -> next-block delta.
      { auto it=temp.find(hist); if(it!=temp.end()&&it->second.c0){ uint64_t pib=(uint64_t)((int64_t)ib[i]+it->second.d0);
          auto xi=xl.find(pib>>6); if(xi!=xl.end()){ uint64_t phys=(xi->second<<6)|(pib&63);
            int j=hit_soon(phys,i); if(j>=0) ct[j]=1; } } }
      bg.operate(ib[i], pb[i], issbg);
      pmap[page]|=(uint64_t{1}<<off); xl[page]=pb[i]>>6; xl[ib[i]>>6]=pb[i]>>6;
      if(hv) temp[hist].obs((int64_t)ib[i]-(int64_t)p1);
      hist=((hist<<16)|(Hh(ib[i])&0xFFFF)) & 0xFFFFFFFFFFFFull; // shift-register of last 3 block hashes
      p1=ib[i]; hv=true;
    }
    uint64_t cb=0,bs=0,bt=0; for(uint32_t j=0;j<N;++j){ if(cbg[j])++cb; if(cbg[j]||cs[j])++bs; if(cbg[j]||ct[j])++bt; }
    double pmk=pmap.size()*64/8.0/1024.0;
    std::printf("  IDEA2 spatial(naive): +%.1fpp (%.1f->%.1f%%) acc=%.1f%% pf/dem=%.2f pmapKiB=%.0f(%zu pages) | IDEA3 temporal: +%.1fpp (->%.1f%%)\n",
      100.0*(bs-cb)/N, 100.0*cb/N, 100.0*bs/N, s_iss?100.0*s_cor/s_iss:0.0, (double)s_iss/N, pmk, pmap.size(), 100.0*(bt-cb)/N, 100.0*bt/N);
  }
}

} // namespace

// CACHE-BASED data evaluation (the PROPER metric). Both berti and SPPAM prefetches FILL a shared L2; demands
// hit/miss against it. Coverage = demand-miss REDUCTION over the berti-ONLY baseline. Usefulness measured per
// source (berti hits DO count toward prefetch usefulness). Region access-map is BOUNDED (contention real).
static void eval_data_cache(const std::string& trace,uint64_t maxrec,int rsets,int rways,int bg_log2,int bg_order,bool amap_evict){
  struct Ev{ uint64_t cyc,b,ip; uint8_t kind; }; // kind 0=demand(LOAD/RFO), 1=berti prefetch
  std::vector<Ev> ev; ev.reserve(1u<<22);
  { trace_reader tr(trace); access_record r; uint64_t n=0;
    while(tr.next(r)){ auto t=(atype)r.type;
      if(t==atype::PREFETCH){ ev.push_back({r.cycle,r.paddr>>6,0,1}); if(maxrec&&++n>=maxrec)break; continue; }
      if(t!=atype::LOAD&&t!=atype::RFO)continue; if((r.ip>>6)==(r.vaddr>>6))continue; // skip instr fetches
      ev.push_back({r.cycle,r.paddr>>6,r.ip,0}); if(maxrec&&++n>=maxrec)break; } }
  std::stable_sort(ev.begin(),ev.end(),[](const Ev&a,const Ev&b){return a.cyc<b.cyc;});
  uint64_t ndem=0; for(auto&e:ev) if(e.kind==0)++ndem;
  if(!ndem){ std::printf("data-cache: no demands in %s\n",trace.c_str()); return; }
  // brackets: NO-prefetch miss (demands only) and BERTI-only miss -> berti's own coverage + usefulness.
  uint64_t nopf_miss=0;
  { L2Cache c(2048,16); for(auto&e:ev){ if(e.kind!=0)continue; if(c.resident(e.b))c.demand_useful(e.b); else{++nopf_miss;c.install(e.b,false);} } }
  uint64_t base_miss=0, bfill0=0, bused0=0;
  { L2Cache c(2048,16); RegionMap pf(1024,12);
    auto fill=[&](uint64_t b){ if(pf.probe(b)||c.resident(b))return false; uint64_t e=c.install(b,true,false); if(e!=L2Cache::NONE)pf.clear_bit(e); pf.set_bit(b); return true; };
    for(auto&e:ev){ if(e.kind==1){ if(fill(e.b))++bfill0; continue; }
      int s=-1,r=c.demand_cov(e.b,&s); if(r<0){++base_miss; uint64_t ev2=c.install(e.b,false,false); if(ev2!=L2Cache::NONE)pf.clear_bit(ev2); pf.set_bit(e.b);} else if(r==1)++bused0; } }
  std::printf("data-cache %s: demands=%llu | no-pf miss=%.1f%%  berti-only miss=%.1f%% (berti cov=%+.1f%%, berti use=%.1f%%)\n",
    trace.substr(trace.rfind('/')+1).c_str(),(unsigned long long)ndem,100.0*nopf_miss/ndem,100.0*base_miss/ndem,
    nopf_miss?100.0*((double)nopf_miss-(double)base_miss)/(double)nopf_miss:0.0, bfill0?100.0*bused0/bfill0:0.0);

  std::vector<EngCfg> cfgs;
  { EngCfg a; a.name="single_nopc"; a.nav="single"; a.pc_bits=0; cfgs.push_back(a);
    EngCfg b; b.name="single_pc8"; b.nav="single"; b.pc_bits=8; cfgs.push_back(b);
    EngCfg d; d.name="single_pc12"; d.nav="single"; d.pc_bits=12; cfgs.push_back(d); }
  std::printf("  region table=%dx%d=%d regions (%.1fKiB). cov=miss-reduction over berti; use=hit-before-evict/fills.\n",
    rsets,rways,rsets*rways,RegionTable(rsets,rways).kib());
  // per-demand class (engine-independent): 0=cold-page (compulsory), 1=spatial (new block in a KNOWN page ->
  // SPPAM's domain), 2=temporal (block demanded before -> evicted & re-missed -> keep/re-predict territory).
  std::vector<uint8_t> dcls(ev.size(),3); long clsN[3]={0};
  { std::unordered_set<uint64_t> sp,sb;
    for(std::size_t i=0;i<ev.size();++i){ if(ev[i].kind!=0)continue; uint64_t b=ev[i].b,pg=b>>6;
      uint8_t cl=!sp.count(pg)?0:(!sb.count(b)?1:2); dcls[i]=cl; ++clsN[cl]; sp.insert(pg); sb.insert(b); } }
  std::printf("  demand mix: cold-page=%.1f%% spatial-in-page=%.1f%% temporal-revisit=%.1f%%\n",
    100.0*clsN[0]/ndem,100.0*clsN[1]/ndem,100.0*clsN[2]/ndem);
  std::printf("  %-13s %8s %10s %10s %8s %9s %8s  %s\n","engine","cov%","sppam_use","berti_use","pf/dem","rgn_evct","phtKiB","uncov-miss[cold/spat/temp]");
  for(auto& cfg:cfgs){
    Engine e(cfg,(uint32_t)ndem);
    L2Cache c(2048,16); RegionMap pf(1024,12); RegionTable rt(rsets,rways); BranchGraph bg((std::size_t)1<<bg_log2,bg_order);
    uint64_t miss=0,sfill=0,sused=0,bfill=0,bused=0,iss=0,prev_ip=0; bool have_prev=false; long umiss[3]={0};
    auto fill=[&](uint64_t b,bool sppam)->bool{ if(pf.probe(b)||c.resident(b))return false; uint64_t evb=c.install(b,true,sppam); if(evb!=L2Cache::NONE){pf.clear_bit(evb); if(amap_evict)rt.clearbit(evb);} pf.set_bit(b); return true; };
    std::vector<std::pair<int,double>> pred; std::unordered_map<int,double> acc;
    for(std::size_t ii=0;ii<ev.size();++ii){ auto& evi=ev[ii];
      if(evi.kind==1){ if(fill(evi.b,false))++bfill; continue; }
      const uint64_t b=evi.b, curip=evi.ip, page=b>>6; const int off=(int)(b&(BPP-1));
      int s=-1,r=c.demand_cov(b,&s);
      if(r<0){ ++miss; ++umiss[dcls[ii]]; uint64_t evb=c.install(b,false,false); if(evb!=L2Cache::NONE){pf.clear_bit(evb); if(amap_evict)rt.clearbit(evb);} pf.set_bit(b); }
      else if(r==1){ if(s==1)++sused; else ++bused; }
      RegionTable::R& R=rt.get(page); const uint64_t prevPC=R.prev_ip; const int ps=cfg.ps;
      for(int d=1;d<=ps;++d){ uint64_t w=window_at(R.amap,off-d,ps); e.pht.train(e.key(w,prevPC),ps-d); }
      { uint64_t spec=R.amap|(uint64_t{1}<<off); int pos=off; uint64_t pc=curip; double pconf=1.0; acc.clear();
        for(int k=0;k<cfg.depth;++k){ uint64_t win=window_at(spec,pos,ps);
          e.pht.lookup(e.key(win,pc),cfg.conf_min,cfg.topk,pred); if(pred.empty())break;
          for(auto&[dl,cf]:pred){ int q=pos+dl; if(q<=off||q>=BPP)continue;
            if(cfg.issue=="accum") acc[q]+=pconf*cf; else { ++iss; if(fill((page<<6)|(uint64_t)q,true))++sfill; } }
          auto&[bd,bc]=pred.front(); if(bc<cfg.advance_min)break; int nx=pos+bd; if(nx<=pos||nx>=BPP)break;
          spec|=(uint64_t{1}<<nx); pos=nx; pconf*=bc*cfg.decay; pc=bg_order>0?bg.predict(pc>>6)<<6:pc; }
        if(cfg.issue=="accum") for(auto&[q,w]:acc){ if(w<cfg.issue_thresh)continue; ++iss; if(fill((page<<6)|(uint64_t)q,true))++sfill; } }
      R.amap|=(uint64_t{1}<<off); R.prev_ip=curip; if(have_prev)bg.train(prev_ip>>6,curip>>6); prev_ip=curip; have_prev=true;
    }
    double cov=base_miss?100.0*((double)base_miss-(double)miss)/(double)base_miss:0.0;
    double um=(double)(umiss[0]+umiss[1]+umiss[2]); if(um<1)um=1;
    char ub[64]; std::snprintf(ub,sizeof ub,"%.0f/%.0f/%.0f",100*umiss[0]/um,100*umiss[1]/um,100*umiss[2]/um);
    std::printf("  %-13s %7.2f%% %9.1f%% %9.1f%% %8.3f %9llu %7.1f  %s\n",
      cfg.name.c_str(),cov, sfill?100.0*sused/sfill:0.0, bfill?100.0*bused/bfill:0.0,
      (double)iss/ndem,(unsigned long long)rt.evicts, e.pht.state_bits(e.keybits(),cfg.topk)/8.0/1024.0, ub);
  }
}

int main(int argc,char**argv){
  std::string trace, cfgpath, l1i_trace, l1d_trace; uint32_t W=256; uint64_t maxrec=0; int bg_order=1, bg_log2=16, scrape_period=8;
  bool oracle=false, amap_evict=false; int rsets=256, rways=12;   // data eval defaults to CACHE model; --oracle=old path; --amap-evict=couple amap to residency
  for(int i=1;i<argc;i++){ std::string a=argv[i]; auto nx=[&]{return std::string(argv[++i]);};
    if(a=="--trace")trace=nx(); else if(a=="--configs")cfgpath=nx(); else if(a=="--l1i-trace")l1i_trace=nx();
    else if(a=="--l1d-trace")l1d_trace=nx();
    else if(a=="--horizon")W=(uint32_t)std::stoul(nx()); else if(a=="--max-records")maxrec=std::stoull(nx());
    else if(a=="--bg-order")bg_order=std::stoi(nx()); else if(a=="--bg-log2")bg_log2=std::stoi(nx());
    else if(a=="--scrape-period")scrape_period=std::stoi(nx());
    else if(a=="--oracle")oracle=true; else if(a=="--rsets")rsets=std::stoi(nx()); else if(a=="--rways")rways=std::stoi(nx());
    else if(a=="--amap-evict")amap_evict=true;
    else { std::fprintf(stderr,"unknown arg %s\n",a.c_str()); return 2; } }
  if(!l1i_trace.empty() && !l1d_trace.empty()){ eval_instr_correlate(l1i_trace,l1d_trace,W,maxrec); return 0; }
  if(!l1i_trace.empty()){ eval_l1i(l1i_trace,W,maxrec,16,bg_log2); if(trace.empty())return 0; }
  if(!trace.empty() && !oracle){ eval_data_cache(trace,maxrec,rsets,rways,bg_log2,bg_order,amap_evict); return 0; }
  if(trace.empty()){ std::fprintf(stderr,"need --trace\n"); return 2; }

  // 1) read the full L2 stream once. Demands = LOAD/RFO (the residual berti left, coverage
  //    targets). Berti's PREFETCH records are tracked in DEMAND-index space (the # of demands
  //    seen so far) so we can score redundancy + incremental coverage against them.
  std::vector<uint64_t> blk, ip;                                   // demands
  std::unordered_map<uint64_t,std::vector<uint32_t>> berti_pf;     // block -> demand-indices at which berti prefetched it
  uint64_t berti_total=0;
  { trace_reader tr(trace); access_record r;
    while(tr.next(r)){ auto t=(atype)r.type;
      if(t==atype::PREFETCH){ berti_pf[r.paddr>>6].push_back((uint32_t)blk.size()); ++berti_total; continue; }
      if(t!=atype::LOAD&&t!=atype::RFO)continue;
      if((r.ip>>6)==(r.vaddr>>6))continue;             // skip instruction fetches
      blk.push_back(r.paddr>>6); ip.push_back(r.ip);
      if(maxrec&&blk.size()>=maxrec)break; } }
  const uint32_t N=(uint32_t)blk.size();
  // 2) oracle index: demand block -> ascending future demand-indices.
  std::unordered_map<uint64_t,std::vector<uint32_t>> idx; idx.reserve(N);
  for(uint32_t i=0;i<N;++i) idx[blk[i]].push_back(i);
  auto hit_soon=[&](uint64_t b,uint32_t i)->int{ auto it=idx.find(b); if(it==idx.end())return -1;
    const auto&v=it->second; auto p=std::upper_bound(v.begin(),v.end(),i);
    return (p!=v.end()&&*p<=i+W)?(int)*p:-1; };
  // berti prefetched block b in demand-window [lo,hi]?  (redundancy + incremental coverage)
  auto berti_between=[&](uint64_t b,long lo,long hi)->bool{ auto it=berti_pf.find(b); if(it==berti_pf.end())return false;
    const auto&v=it->second; auto p=std::lower_bound(v.begin(),v.end(),(uint32_t)std::max(0L,lo));
    return p!=v.end() && (long)*p<=hi; };
  // a demand is "berti-covered" if berti prefetched its block within the prior W demands (in time).
  std::vector<uint8_t> berti_cov(N,0); uint64_t bcov=0;
  for(uint32_t i=0;i<N;++i){ if(berti_between(blk[i],(long)i-(long)W,(long)i-1)){ berti_cov[i]=1; ++bcov; } }
  std::fprintf(stderr,"demands=%u unique_blocks=%zu berti_pf=%llu berti_covers=%.1f%% horizon=%u\n",
    N,idx.size(),(unsigned long long)berti_total,N?100.0*bcov/N:0.0,W);

  // 3) engines.
  std::vector<EngCfg> cfgs;
  if(cfgpath.empty()){
    EngCfg a; a.name="scan_nopc"; a.nav="scan"; a.pc_bits=0; a.issue="perstep"; cfgs.push_back(a);
    EngCfg b; b.name="single_nopc"; b.nav="single"; b.pc_bits=0; cfgs.push_back(b);
    EngCfg c; c.name="single_pc8"; c.nav="single"; c.pc_bits=8; cfgs.push_back(c);
  } else { nlohmann::json j; std::ifstream f(cfgpath); f>>j;
    for(auto&o:j){ EngCfg e; e.name=o.value("name","cfg");
      e.ps=o.value("ps",e.ps); e.pc_bits=o.value("pc_bits",e.pc_bits); e.nav=o.value("nav",e.nav);
      e.train=o.value("train",e.train);
      e.topk=o.value("topk",e.topk); e.issue=o.value("issue",e.issue); e.conf_min=o.value("conf_min",e.conf_min);
      e.issue_thresh=o.value("issue_thresh",e.issue_thresh); e.advance_min=o.value("advance_min",e.advance_min);
      e.depth=o.value("depth",e.depth); e.decay=o.value("decay",e.decay); cfgs.push_back(e); } }
  std::vector<Engine> eng; eng.reserve(cfgs.size());
  for(auto&c:cfgs) eng.emplace_back(c,N);

  // shared region map + branch graph (rebuilt per replay; region map shared across engines
  // since it is just the observed access history).
  std::unordered_map<uint64_t,Region> regions; regions.reserve(1<<16);
  BranchGraph bg((std::size_t)1<<bg_log2, bg_order);
  uint64_t prev_ip=0; bool have_prev=false;

  std::vector<std::pair<int,double>> pred; pred.reserve(16);
  std::unordered_map<int,double> acc; acc.reserve(64);
  // score one issued prefetch (block pb) at demand i: count issue, oracle hit (coverage/accuracy),
  // and redundancy with berti (pb prefetched by berti within +-W of this demand).
  auto score=[&](Engine& e,uint64_t pb,uint32_t i){
    ++e.issued; int j=hit_soon(pb,i); if(j>=0){ ++e.correct; e.covered[j]=1; }
    if(berti_between(pb,(long)i-(long)W,(long)i+(long)W)) ++e.redun;
  };

  for(uint32_t i=0;i<N;++i){
    const uint64_t b=blk[i], curip=ip[i]; const uint64_t page=b>>6; const int off=(int)(b&(BPP-1));
    Region& R=regions[page]; const uint64_t prevPC=R.prev_ip;
    const bool do_scrape=(++R.since_scrape>=(uint32_t)scrape_period);

    for(auto& e:eng){
      const int ps=e.c.ps;
      // ---- train ----
      if(e.c.train=="scrape"){
        // batch the accumulated region map every scrape_period accesses: for each start pos,
        // train (window -> every accessed block ahead). PC (if any) is the region's single
        // pc_pht -> ALL patterns alias to one PC (why scrape can't carry per-access PC context).
        if(do_scrape) for(int p=0;p<BPP;++p){ uint64_t w=window_at(R.amap,p,ps);
          for(int a2=1;a2<=ps;++a2){ int q=p+a2; if(q<BPP && ((R.amap>>q)&1)) e.pht.train(e.key(w,prevPC), ps-a2); } }
      } else {
        // online: train on PRIOR map, keyed by the PREVIOUS accessing PC.
        for(int d=1; d<=ps; ++d){ uint64_t w=window_at(R.amap, off-d, ps); e.pht.train(e.key(w,prevPC), ps-d); }
      }
      // ---- predict ----
      if(e.c.nav=="scan"){
        // SPPAM-style multi-start scan (no PC): each start position issues its predicted bits.
        for(int s=0;s<e.c.depth;++s){ int p=off+s; if(p<0||p>=BPP)break;
          e.pht.lookup(e.key(window_at(R.amap,p,ps),prevPC), e.c.conf_min, e.c.topk, pred);
          for(auto&[dl,cf]:pred){ int q=p+dl; if(q<=off||q>=BPP)continue;
            score(e,(page<<6)|(uint64_t)q,i); } }
      } else {
        // single deep path: advance strongest delta, PC via branch graph, window from specmap.
        uint64_t spec=R.amap | (uint64_t{1}<<off); int pos=off; uint64_t pc=curip; double pconf=1.0;
        acc.clear();
        for(int k=0;k<e.c.depth;++k){
          uint64_t win=window_at(spec,pos,ps);
          e.pht.lookup(e.key(win,pc), e.c.conf_min, e.c.topk, pred);
          if(pred.empty())break;
          for(auto&[dl,cf]:pred){ int q=pos+dl; if(q<=off||q>=BPP)continue;
            if(e.c.issue=="accum") acc[q]+=pconf*cf;
            else score(e,(page<<6)|(uint64_t)q,i); }
          // advance along strongest
          auto&[bd,bc]=pred.front(); if(bc<e.c.advance_min)break;
          int nx=pos+bd; if(nx<=pos||nx>=BPP)break;
          spec|=(uint64_t{1}<<nx); pos=nx; pconf*=bc*e.c.decay;
          pc = bg_order>0 ? bg.predict(pc>>6)<<6 : pc;   // future load-PC (block-granular)
        }
        if(e.c.issue=="accum"){
          for(auto&[q,w]:acc){ if(w<e.c.issue_thresh)continue; score(e,(page<<6)|(uint64_t)q,i); }
        }
      }
    }
    // ---- commit access to shared state (after all engines trained/predicted on prior state) ----
    if(do_scrape) R.since_scrape=0;
    R.amap|=(uint64_t{1}<<off); R.prev_ip=curip; R.seen=true;
    if(have_prev) bg.train(prev_ip>>6, curip>>6); prev_ip=curip; have_prev=true;
  }

  // 4) per-class denominators (computed once over the demand stream; class is engine-independent).
  long CLS_TOT[5]={0};
  { std::unordered_map<uint64_t,std::pair<int,int>> last; std::unordered_map<uint64_t,uint64_t> amp;
    for(uint32_t i=0;i<N;++i){ uint64_t pg=blk[i]>>6; int off=(int)(blk[i]&(BPP-1));
      bool seen=amp.count(pg); int cat;
      if(!seen)cat=0; else { auto&mp=amp[pg]; auto it=last.find(pg); int lo=it!=last.end()?it->second.first:-1; int ld=it!=last.end()?it->second.second:1000;
        if((mp>>off)&1)cat=3; else {int d=off-lo; cat=(d==ld)?1:4;} last[pg]={off,off-lo}; }
      amp[pg]|=(uint64_t{1}<<off); ++CLS_TOT[cat]; } }
  std::printf("  demand-class distribution (of residual): cold=%.1f%% stride=%.1f%% recur=%.1f%% revisit=%.1f%% novel=%.1f%%  [cold=compulsory, stride/novel=spatial-in-page, revisit=temporal]\n",
    100.0*CLS_TOT[0]/N,100.0*CLS_TOT[1]/N,100.0*CLS_TOT[2]/N,100.0*CLS_TOT[3]/N,100.0*CLS_TOT[4]/N);
  // 5) tally + report. cov = residual demands covered; incr = covered AND berti did NOT cover
  //    (SPPAM's added value alongside berti); redun = issued blocks berti also fetched.
  std::printf("%-14s %7s %7s %7s %7s %8s %8s %9s  %s\n",
    "engine","cov%","incr%","acc%","redun%","pf/dem","phtKiB","bgKiB","incrcov_class[cold/stride/recur/revisit/novel]");
  for(auto& e:eng){
    uint64_t cov=0, incr=0; long ccov[5]={0};
    { std::unordered_map<uint64_t,std::pair<int,int>> last; std::unordered_map<uint64_t,uint64_t> amp;
      for(uint32_t i=0;i<N;++i){ uint64_t pg=blk[i]>>6; int off=(int)(blk[i]&(BPP-1));
        bool seen=amp.count(pg); int cat;
        if(!seen)cat=0; else { auto&mp=amp[pg]; auto it=last.find(pg); int lo=it!=last.end()?it->second.first:-1; int ld=it!=last.end()?it->second.second:1000;
          if((mp>>off)&1)cat=3; else {int d=off-lo; cat=(d==ld)?1:4;} last[pg]={off,off-lo}; }
        amp[pg]|=(uint64_t{1}<<off);
        if(e.covered[i]){ ++cov; if(!berti_cov[i]){ ++incr; ++ccov[cat]; } } } }
    double covp=100.0*cov/N, incrp=100.0*incr/N, accp=e.issued?100.0*e.correct/e.issued:0.0;
    double redp=e.issued?100.0*e.redun/e.issued:0.0, pfd=(double)e.issued/N;
    double phtk=e.pht.state_bits(e.keybits(),e.c.topk)/8.0/1024.0;
    double bgk=(e.c.nav=="single"&&bg_order>0)?bg.state_bits()/8.0/1024.0:0.0;
    char cb[128]; std::snprintf(cb,sizeof cb,"%.0f/%.0f/%.0f/%.0f/%.0f",
      CLS_TOT[0]?100.0*ccov[0]/CLS_TOT[0]:0, CLS_TOT[1]?100.0*ccov[1]/CLS_TOT[1]:0,
      CLS_TOT[2]?100.0*ccov[2]/CLS_TOT[2]:0, CLS_TOT[3]?100.0*ccov[3]/CLS_TOT[3]:0,
      CLS_TOT[4]?100.0*ccov[4]/CLS_TOT[4]:0);
    std::printf("%-14s %6.2f%% %6.2f%% %6.1f%% %6.1f%% %8.3f %8.1f %9.1f  %s\n",
      e.c.name.c_str(),covp,incrp,accp,redp,pfd,phtk,bgk,cb);
  }
  return 0;
}
