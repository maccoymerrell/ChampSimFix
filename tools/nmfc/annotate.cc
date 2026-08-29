/**
 * Turn a ChampSim trace of a compiled benchmark into an NMFC trace.
 *
 * Nothing here invents an instruction, a program counter or a register. The
 * compiler decided what each function contains, the linker decided where it
 * lives, and Pin recorded what actually executed. This pass only says which
 * instructions belonged to which invocation, where the data should live in the
 * simulated address space, and refuses to emit anything the machine could not
 * run.
 *
 * Inputs, all captured from one build by tools/nmfc/kernels/trace.sh:
 *   trace.champsimtrace   what executed
 *   symbols.txt           name, address, size of each offloadable function
 *   atomics.txt           program counters of locked instructions
 *   regions.txt           where each array really lives
 *   regmap.txt            Pin register id -> canonical register (see regmap_dump)
 */
#include "nmfc/nmfc_trace.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

namespace
{

struct options {
  std::string trace, symbols, atomics, rets, waits, commits, regions, regmap, out;
  std::uint32_t tiles = 4;
  std::uint32_t grain_bits = 20;
  std::uint32_t page_bits = 12;
  std::uint32_t block_bits = 6;
  std::uint64_t sim_base = 0x2000'0000ULL;
  std::uint64_t aperture_base = std::uint64_t{1} << 46;
  std::uint32_t num_regs = nmfc::MAX_FUNCTION_REGS;
};

struct region_t {
  std::string name;
  std::uint64_t real_base = 0, real_end = 0, sim_base = 0;
  nmfc::region kind = nmfc::region::NMFC;
};

struct function_t {
  std::string name;
  std::uint64_t start = 0, end = 0;
  std::uint32_t id = 0;
};

[[noreturn]] void die(const std::string& what)
{
  std::fprintf(stderr, "[annotate] ERROR: %s\n", what.c_str());
  std::exit(1);
}

/** ChampSim's on-disk instruction; must match the tracer's record. */
struct input_instr {
  unsigned long long ip;
  unsigned char is_branch;
  unsigned char branch_taken;
  unsigned char destination_registers[nmfc::NUM_DESTINATIONS];
  unsigned char source_registers[nmfc::NUM_SOURCES];
  unsigned long long destination_memory[nmfc::NUM_DESTINATIONS];
  unsigned long long source_memory[nmfc::NUM_SOURCES];
};
static_assert(sizeof(input_instr) == 64, "must match the Pin tracer's output record");

std::vector<region_t> load_regions(const options& opt)
{
  std::ifstream in(opt.regions);
  if (!in) {
    die("cannot open regions file " + opt.regions);
  }
  std::vector<region_t> regions;
  std::string name;
  std::uint64_t base = 0, bytes = 0;
  while (in >> name >> std::hex >> base >> bytes >> std::dec) {
    regions.push_back(region_t{name, base, base + bytes, 0, nmfc::region::NMFC});
  }
  if (regions.empty()) {
    die("no regions declared; the benchmark must write a manifest");
  }
  return regions;
}

/**
 * Give every region a simulated base, preserving its grain's tile number.
 *
 * The benchmark decides which tile owns an address by the same arithmetic the
 * simulator uses, and it does so on *real* addresses at trace time -- that is
 * how a bucket lane ends up holding only vertices whose parent entry lives on
 * one tile. If remapping shifted a region's grain index by a different amount
 * than another's, that property would quietly break: a lane would span two
 * tiles and the consumer would migrate per element.
 *
 * So every region is placed at a grain whose index is congruent, modulo the
 * tile count, to the grain it really occupied. All tile relationships the
 * program computed then survive the move as a single rotation.
 */
void place_regions(std::vector<region_t>& regions, const options& opt)
{
  const std::uint64_t grain = std::uint64_t{1} << opt.grain_bits;
  std::uint64_t next = (opt.sim_base + grain - 1) & ~(grain - 1);
  for (auto& r : regions) {
    const std::uint64_t real_grain = r.real_base >> opt.grain_bits;
    while ((next >> opt.grain_bits) % opt.tiles != real_grain % opt.tiles) {
      next += grain;
    }
    r.sim_base = next;
    const std::uint64_t span = r.real_end - r.real_base;
    next += ((span + grain - 1) / grain) * grain;
    if (next > opt.aperture_base) {
      die("simulated regions ran into the offload aperture");
    }
  }
}

std::vector<function_t> load_functions(const options& opt)
{
  std::ifstream in(opt.symbols);
  if (!in) {
    die("cannot open symbols file " + opt.symbols);
  }
  std::vector<function_t> fns;
  std::string name;
  std::uint64_t addr = 0, size = 0;
  while (in >> name >> std::hex >> addr >> std::dec >> size) {
    if (size == 0) {
      continue;
    }
    fns.push_back(function_t{name, addr, addr + size, static_cast<std::uint32_t>(fns.size())});
  }
  if (fns.empty()) {
    die("no offloadable functions found; nothing to annotate");
  }
  std::sort(fns.begin(), fns.end(), [](const auto& a, const auto& b) { return a.start < b.start; });
  for (std::size_t i = 0; i < fns.size(); ++i) {
    fns[i].id = static_cast<std::uint32_t>(i);
    if (i > 0 && fns[i].start < fns[i - 1].end) {
      die("functions " + fns[i - 1].name + " and " + fns[i].name + " overlap; identical-code folding was not disabled");
    }
  }
  return fns;
}

std::set<std::uint64_t> load_atomics(const options& opt)
{
  std::ifstream in(opt.atomics);
  std::set<std::uint64_t> pcs;
  std::uint64_t pc = 0;
  while (in >> std::hex >> pc >> std::dec) {
    pcs.insert(pc);
  }
  return pcs;
}

/** Pin register id -> canonical register, with the ones the machine has no file for removed. */
std::unordered_map<std::uint32_t, std::uint32_t> load_regmap(const options& opt, std::uint32_t& result_reg)
{
  std::ifstream in(opt.regmap);
  if (!in) {
    die("cannot open regmap file " + opt.regmap);
  }
  std::unordered_map<std::uint32_t, std::uint32_t> canon;
  result_reg = 0;
  // Line-wise, because some register names contain spaces -- Pin writes
  // "*UNKNOWN REG 2*" for gaps in the enumeration, and reading token by token
  // walks the stream out of alignment from there on, silently.
  std::string line;
  while (std::getline(in, line)) {
    std::vector<std::string> field;
    std::size_t at = 0;
    while (at < line.size()) {
      const auto begin = line.find_first_not_of(" \t", at);
      if (begin == std::string::npos) {
        break;
      }
      const auto end = line.find_first_of(" \t", begin);
      field.push_back(line.substr(begin, end == std::string::npos ? std::string::npos : end - begin));
      at = (end == std::string::npos) ? line.size() : end;
    }
    if (field.size() < 4) {
      continue;
    }
    const std::uint32_t id = static_cast<std::uint32_t>(std::stoul(field[0]));
    const std::uint32_t full = static_cast<std::uint32_t>(std::stoul(field[1]));
    const std::string& full_name = field.back();
    if (full_name == "rax") {
      result_reg = full; // where a function's return value arrives
    }
    // The program counter travels beside the register file, this machine has no
    // stack, and flags are internal to an instruction. None is regfile state.
    if (full_name == "rip" || full_name == "rsp" || full_name == "rflags" || full_name == "*invalid*") {
      continue;
    }
    canon[id] = full;
  }
  return canon;
}

} // namespace

int main(int argc, char** argv)
{
  options opt;
  for (int i = 1; i < argc; ++i) {
    const std::string a = argv[i];
    const auto next = [&]() -> std::string { return (i + 1 < argc) ? argv[++i] : std::string{}; };
    if (a == "--trace") { opt.trace = next(); }
    else if (a == "--symbols") { opt.symbols = next(); }
    else if (a == "--atomics") { opt.atomics = next(); }
    else if (a == "--rets") { opt.rets = next(); }
    else if (a == "--waits") { opt.waits = next(); }
    else if (a == "--commits") { opt.commits = next(); }
    else if (a == "--regions") { opt.regions = next(); }
    else if (a == "--regmap") { opt.regmap = next(); }
    else if (a == "--out") { opt.out = next(); }
    else if (a == "--tiles") { opt.tiles = std::stoul(next()); }
    else if (a == "--grain-bits") { opt.grain_bits = std::stoul(next()); }
    else if (a == "--page-bits") { opt.page_bits = std::stoul(next()); }
    else if (a == "--block-bits") { opt.block_bits = std::stoul(next()); }
    else { die("unknown argument " + a); }
  }
  if (opt.trace.empty() || opt.out.empty()) {
    die("usage: annotate --trace T --symbols S --atomics A --regions R --regmap M --out O");
  }

  auto regions = load_regions(opt);
  place_regions(regions, opt);
  const auto functions = load_functions(opt);
  const auto atomic_pcs = load_atomics(opt);
  options ret_opt = opt;
  ret_opt.atomics = opt.rets;
  const auto ret_pcs = load_atomics(ret_opt);
  std::uint32_t result_reg = 0;
  const auto canon = load_regmap(opt, result_reg);

  // The memory-committing loop's wait site, as a program-counter range: the
  // block belongs where the caller said it does, not at whichever load of the
  // output happens to come first.
  std::vector<std::pair<std::uint64_t, std::uint64_t>> wait_pcs;
  if (!opt.waits.empty()) {
    std::ifstream win(opt.waits);
    std::uint64_t a = 0, n = 0;
    while (win >> std::hex >> a >> std::dec >> n) {
      wait_pcs.emplace_back(a, a + n);
    }
  }
  options commit_opt = opt;
  commit_opt.atomics = opt.commits;
  const auto commit_pcs = load_atomics(commit_opt);

  const auto is_wait = [&](std::uint64_t ip) {
    for (const auto& [lo, hi] : wait_pcs) {
      if (ip >= lo && ip < hi) {
        return true;
      }
    }
    return false;
  };
  if (result_reg == 0) {
    die("the register map does not name a return register");
  }

  // The code the functions occupy is itself a region: one virtual page,
  // replicated per channel, with the tile chosen when it is translated.
  region_t code{"code", functions.front().start, functions.back().end, 0, nmfc::region::CODE};
  {
    const std::uint64_t grain = std::uint64_t{1} << opt.grain_bits;
    code.real_base &= ~(grain - 1);
    code.real_end = ((code.real_end + grain - 1) / grain) * grain;
    std::vector<region_t> one{code};
    options shifted = opt;
    shifted.sim_base = regions.back().sim_base + (1ULL << opt.grain_bits);
    place_regions(one, shifted);
    code = one.front();
  }

  std::fprintf(stderr, "[annotate] %zu functions, %zu regions, %zu atomic sites\n", functions.size(), regions.size(), atomic_pcs.size());
  for (const auto& r : regions) {
    std::fprintf(stderr, "  %-10s real %#012lx  ->  sim %#012lx  %8.2f MiB  tile %lu\n", r.name.c_str(), r.real_base, r.sim_base,
                 double(r.real_end - r.real_base) / (1024 * 1024), (r.sim_base >> opt.grain_bits) % opt.tiles);
  }
  std::fprintf(stderr, "  %-10s real %#012lx  ->  sim %#012lx  replicated on every channel\n", "code", code.real_base, code.sim_base);

  // ---- address remapping -------------------------------------------------

  std::vector<region_t> all = regions;
  all.push_back(code);
  const auto remap = [&](std::uint64_t addr) -> std::optional<std::uint64_t> {
    if (addr == 0) {
      return std::uint64_t{0};
    }
    for (const auto& r : all) {
      if (addr >= r.real_base && addr < r.real_end) {
        return r.sim_base + (addr - r.real_base);
      }
    }
    return std::nullopt;
  };

  // ---- output ------------------------------------------------------------

  std::ofstream out(opt.out, std::ios::binary);
  if (!out) {
    die("cannot write " + opt.out);
  }
  nmfc::header hdr{};
  hdr.magic = nmfc::TRACE_MAGIC;
  hdr.version = nmfc::TRACE_VERSION;
  hdr.record_size = sizeof(nmfc::record);
  hdr.num_regs = opt.num_regs;
  hdr.num_tiles = opt.tiles;
  hdr.page_size = 1U << opt.page_bits;
  hdr.block_size = 1U << opt.block_bits;
  hdr.interleave_shift = opt.grain_bits;
  hdr.num_asids = 1;
  out.write(reinterpret_cast<const char*>(&hdr), sizeof(hdr));

  std::uint64_t n_records = 0, n_calls = 0;
  const auto put = [&](const nmfc::record& r) {
    out.write(reinterpret_cast<const char*>(&r), sizeof(r));
    ++n_records;
  };

  // Placement hints first: where the pseudo-compiler would like each page to
  // live. They are hints -- NMFC_VMEM picks the frame, and under the
  // translate-first routers it may pick a different tile or move it later.
  for (const auto& r : all) {
    const std::uint64_t page = 1ULL << opt.page_bits;
    for (std::uint64_t a = r.sim_base; a < r.sim_base + (r.real_end - r.real_base); a += page) {
      nmfc::record hint{};
      hint.kind = static_cast<std::uint8_t>(nmfc::op::PAGE_HINT);
      hint.aux0 = a >> opt.page_bits;
      const std::uint32_t tile = static_cast<std::uint32_t>((a >> opt.grain_bits) % opt.tiles);
      hint.aux1 = nmfc::encode_page_hint(0, r.kind, tile);
      put(hint);
    }
  }

  // ---- stream ------------------------------------------------------------

  std::ifstream tin(opt.trace, std::ios::binary);
  if (!tin) {
    die("cannot open " + opt.trace);
  }

  const auto find_fn = [&](std::uint64_t ip) -> const function_t* {
    for (const auto& f : functions) {
      if (ip >= f.start && ip < f.end) {
        return &f;
      }
    }
    return nullptr;
  };

  const function_t* cur = nullptr;
  std::vector<nmfc::record> body;
  nmfc::record call_rec{};
  bool have_call = false;
  nmfc::record pending{};
  bool have_pending = false;
  std::uint64_t token = 0;
  std::unordered_map<std::uint32_t, std::uint8_t> slot;
  std::uint64_t dropped_stack = 0, n_bodies = 0, n_atomics = 0, n_joins = 0;
  std::uint64_t awaiting = 0;      // token whose result has not been consumed yet
  // A finished invocation is held for a few host instructions before it is
  // written, because whether it is a fork-and-wait or a fire-and-forget is
  // decided by what comes next: if nothing reads the value it produced, the
  // host never waits for it, and marking it deferred-join would leave a
  // tracking-unit entry outstanding for an answer that never arrives.
  // An invocation that returns nothing still has to be waited for: the host
  // reads what it wrote. That read is the join -- a real instruction, and the
  // only point at which the host actually needs the work to have finished.
  //
  // Without it the trace is silently optimistic. In the traced program the call
  // was synchronous, so the output was always already there and a polling loop
  // would spin zero times; nothing about the wait survives into the trace, and
  // a trace-driven simulator cannot invent it because it does not model values.
  // Ownership, by address: which invocation committed a block, and whether
  // anyone has blocked on it yet. Both directions are checked -- an invocation
  // that commits twice, or a caller that blocks twice on one commit, is a
  // broken pairing and the trace is refused rather than emitted.
  // Ownership covers every block the invocation wrote, not just the one its
  // commit store touched. It has to: the commit lands at the terminator, whose
  // address depends on how much was claimed, while the caller waits on the
  // slot base -- the function cannot commit at the base without keeping it
  // live across the whole loop, which is a ninth register it does not have.
  std::unordered_map<std::uint64_t, std::uint64_t> wrote; // block -> owning token
  std::unordered_map<std::uint64_t, std::vector<std::uint64_t>> owns; // token -> its blocks
  std::vector<std::uint64_t> touched; // blocks written by the invocation being built
  bool commit_pending = false; // the marker was seen; the next store publishes
  std::uint64_t n_commits = 0;
  std::uint64_t n_mem_joins = 0;
  bool holding = false;
  nmfc::record held_call{};
  std::vector<nmfc::record> held_body;
  std::vector<nmfc::record> held_after; // host records seen while holding
  std::uint64_t n_forget = 0;
  constexpr int LOOKAHEAD = 4;

  const auto reg_slot = [&](unsigned char raw_reg) -> unsigned char {
    if (raw_reg == 0) {
      return 0;
    }
    const auto it = canon.find(raw_reg);
    if (it == canon.end()) {
      return 0; // pc, stack pointer or flags: not register-file state
    }
    auto [pos, inserted] = slot.emplace(it->second, static_cast<std::uint8_t>(slot.size() + 1));
    if (inserted && slot.size() > opt.num_regs) {
      die("function " + std::string(cur != nullptr ? cur->name : "?") + " needs more than " + std::to_string(opt.num_regs) +
          " registers live; it cannot run on this machine. Rewrite it or reject it -- do not truncate, "
          "which would drop dependencies and flatter the scoreboard.");
    }
    return pos->second;
  };

  // Write a held invocation, now that its kind is known.
  const auto emit_held = [&](bool no_return) {
    if (!holding) {
      return;
    }
    // Deferred, never fire-and-forget. An invocation that returns nothing in
    // registers still writes output the host reads, and that read is its join;
    // marking it NO_RETURN would complete it at dispatch and leave the later
    // join referring to an entry that no longer exists. `no_return` here only
    // records that nothing consumed its *register* result nearby.
    if (no_return) {
      ++n_forget;
    }
    put(held_call);
    ++n_calls;
    for (const auto& r : held_body) {
      put(r);
    }
    nmfc::record ret{};
    ret.kind = static_cast<std::uint8_t>(nmfc::op::RET);
    ret.token = held_call.token;
    // The whole register file comes home: 512 bits in, 512 bits out.
    ret.aux0 = opt.num_regs;
    put(ret);
    for (const auto& r : held_after) {
      put(r);
    }
    held_body.clear();
    held_after.clear();
    holding = false;
    ++n_bodies;
  };

  const auto flush_body = [&]() {
    if (!have_call || body.empty()) {
      return;
    }
    emit_held(true); // an invocation still held when the next one starts was never waited on
    call_rec.aux1 = nmfc::encode_call_aux1(cur != nullptr ? cur->id : 0, static_cast<std::uint32_t>(body.size()));
    held_call = call_rec;
    held_body = body;
    holding = true;
    body.clear();
    have_call = false;
    awaiting = token; // the next instruction to read the result register joins it
  };

  input_instr raw{};
  std::uint64_t n_in = 0;
  while (tin.read(reinterpret_cast<char*>(&raw), sizeof(raw))) {
    ++n_in;
    const function_t* fn = find_fn(raw.ip);

    if (fn != cur) {
      if (cur != nullptr) {
        flush_body();
      }
      if (fn != nullptr) {
        // The instruction just before entry is the call that made it.
        if (!have_pending) {
          die("a function was entered with no preceding instruction to name as the call");
        }
        call_rec = pending;
        have_pending = false;
        call_rec.kind = static_cast<std::uint8_t>(nmfc::op::CALL);
        // A fork does not wait; the join does. Without this the two collapse
        // into one blocking instruction and the machine holds a single
        // invocation however much room it has.
        call_rec.flag_bits |= nmfc::FLAG_DEFERRED_JOIN;
        call_rec.token = ++token;
        touched.clear();
        const auto entry = remap(fn->start);
        if (!entry) {
          die("function entry point falls outside the code region");
        }
        call_rec.aux0 = *entry;
        have_call = true;
        slot.clear();
      }
      cur = fn;
    }

    nmfc::record rec{};
    rec.instr.ip = raw.ip;
    rec.instr.is_branch = raw.is_branch;
    rec.instr.branch_taken = raw.branch_taken;

    const bool in_body = (fn != nullptr);
    if (in_body) {
      const auto ip = remap(raw.ip);
      if (!ip) {
        die("a function instruction lies outside the code region");
      }
      rec.instr.ip = *ip;
      rec.token = token;
      for (std::size_t i = 0; i < nmfc::NUM_DESTINATIONS; ++i) {
        rec.instr.destination_registers[i] = reg_slot(raw.destination_registers[i]);
      }
      for (std::size_t i = 0; i < nmfc::NUM_SOURCES; ++i) {
        rec.instr.source_registers[i] = reg_slot(raw.source_registers[i]);
      }
    } else {
      std::memcpy(rec.instr.destination_registers, raw.destination_registers, sizeof(raw.destination_registers));
      std::memcpy(rec.instr.source_registers, raw.source_registers, sizeof(raw.source_registers));
    }

    const auto move = [&](unsigned long long src, unsigned long long& dst) {
      const auto m = remap(src);
      if (m) {
        dst = *m;
        return;
      }
      if (!in_body) {
        dst = src; // host stack and library data stay where they are
        return;
      }
      // A function instruction reached something the manifest does not
      // describe. The return is the one legitimate case: x86 pops its return
      // address, and this machine's return is not a stack operation. Pin does
      // not mark ret as a branch, so the return sites come from the
      // disassembly rather than from a guess about the instruction's shape.
      if (ret_pcs.count(raw.ip) != 0) {
        dst = 0;
        ++dropped_stack;
        return;
      }
      die("function instruction at " + std::to_string(raw.ip) + " touches address " + std::to_string(src) +
          ", which no declared region covers -- the manifest is incomplete");
    };
    for (std::size_t i = 0; i < nmfc::NUM_DESTINATIONS; ++i) {
      move(raw.destination_memory[i], rec.instr.destination_memory[i]);
    }
    for (std::size_t i = 0; i < nmfc::NUM_SOURCES; ++i) {
      move(raw.source_memory[i], rec.instr.source_memory[i]);
    }

    bool has_load = false, has_store = false;
    for (const auto a : rec.instr.source_memory) {
      has_load |= (a != 0);
    }
    for (const auto a : rec.instr.destination_memory) {
      has_store |= (a != 0);
    }
    rec.op_class = static_cast<std::uint8_t>(raw.is_branch  ? nmfc::op_class::BRANCH
                                             : has_store    ? nmfc::op_class::STORE
                                             : has_load     ? nmfc::op_class::LOAD
                                                            : nmfc::op_class::ALU);

    if (in_body) {
      // Ownership is over the invocation's *output*, so an atomic is excluded:
      // a read-modify-write is shared state by definition, and two invocations
      // claiming different vertices in one block of parent[] would otherwise
      // each claim to own that block.
      if (atomic_pcs.count(raw.ip) == 0) {
        for (const auto a : rec.instr.destination_memory) {
          if (a != 0) {
            touched.push_back(a >> opt.block_bits);
          }
        }
      }
      if (commit_pcs.count(raw.ip) != 0) {
        if (commit_pending) {
          die("invocation " + std::to_string(token) + " reached a second commit marker before publishing the first");
        }
        commit_pending = true;
      } else if (commit_pending) {
        // The store after the marker publishes. Everything this invocation
        // wrote becomes its, so whichever address the caller waits on resolves.
        commit_pending = false;
        if (owns.count(token) != 0) {
          die("invocation " + std::to_string(token) + " commits twice");
        }
        std::sort(touched.begin(), touched.end());
        touched.erase(std::unique(touched.begin(), touched.end()), touched.end());
        for (const auto blk : touched) {
          const auto prior = wrote.find(blk);
          if (prior != wrote.end() && prior->second != token) {
            die("invocation " + std::to_string(token) + " commits block " + std::to_string(blk) + ", which invocation " +
                std::to_string(prior->second) + " committed and nothing has blocked on -- a double commit");
          }
          wrote[blk] = token;
        }
        owns[token] = touched;
        ++n_commits;
      }
      const bool is_atomic = atomic_pcs.count(raw.ip) != 0;
      rec.kind = static_cast<std::uint8_t>(is_atomic ? nmfc::op::ATOMIC : nmfc::op::BODY);
      n_atomics += is_atomic ? 1 : 0;
      body.push_back(rec);
    } else {
      if (have_pending) {
        if (holding) {
          held_after.push_back(pending);
          if (held_after.size() >= LOOKAHEAD) {
            const auto tail = held_after;
            held_after.clear();
            emit_held(true); // nothing read it within the lookahead
            for (const auto& r : tail) {
              put(r);
            }
          }
        } else {
          put(pending);
        }
      }
      rec.kind = static_cast<std::uint8_t>(nmfc::op::HOST);
      // The marked wait site, and only it: block here until whichever
      // invocation wrote this address has committed it.
      if (is_wait(raw.ip)) {
        for (const auto a : rec.instr.source_memory) {
          if (a == 0) {
            continue;
          }
          const auto it = wrote.find(a >> opt.block_bits);
          if (it == wrote.end()) {
            continue; // this operand is not a committed block; try the next
          }
          {
            rec.kind = static_cast<std::uint8_t>(nmfc::op::JOIN);
            rec.token = it->second;
            // Consumed: the whole invocation's ownership is released, so a
            // second block on any part of it is an error.
            const auto owner = it->second;
            const auto o = owns.find(owner);
            if (o != owns.end()) {
              for (const auto blk : o->second) {
                wrote.erase(blk);
              }
              owns.erase(o);
            } else {
              die("blocked on a commit that was already consumed -- a double block");
            }
            ++n_joins;
            ++n_mem_joins;
            if (holding) {
              emit_held(false);
            }
            break;
          }
        }
      }
      if (rec.kind == static_cast<std::uint8_t>(nmfc::op::HOST) && awaiting != 0 && holding) {
        // The join is a real instruction: whichever one first reads the value
        // the invocation produced. Nothing is synthesised for it.
        for (const auto r : raw.source_registers) {
          const auto it = canon.find(r);
          if (it != canon.end() && it->second == result_reg) {
            rec.kind = static_cast<std::uint8_t>(nmfc::op::JOIN);
            rec.token = awaiting;
            awaiting = 0;
            ++n_joins;
            emit_held(false); // something reads its result, so the host does wait
            break;
          }
        }
      }
      pending = rec;
      have_pending = true;
    }
  }
  if (cur != nullptr) {
    flush_body();
  }
  emit_held(true);
  if (have_pending) {
    put(pending);
  }

  hdr.num_records = n_records;
  hdr.num_calls = n_calls;
  out.seekp(0);
  out.write(reinterpret_cast<const char*>(&hdr), sizeof(hdr));
  out.close();

  std::fprintf(stderr, "[annotate] read %lu instructions -> %lu records, %lu invocations, %lu joins, %lu atomics\n", n_in, n_records, n_calls,
               n_joins, n_atomics);
  std::fprintf(stderr, "[annotate] %lu invocations return no register value; %lu commits, %lu blocks matched to them\n", n_forget, n_commits,
               n_mem_joins);
  if (n_commits != n_mem_joins) {
    std::fprintf(stderr, "[annotate] NOTE: %lu commits were never blocked on (unharvested at trace end)\n", n_commits - n_mem_joins);
  }
  if (dropped_stack != 0) {
    std::fprintf(stderr, "[annotate] dropped %lu stack accesses from returns (this machine has no stack)\n", dropped_stack);
  }

  // ---- validate ----------------------------------------------------------
  //
  // Read back what was written and check it against the format, rather than
  // trusting that the code above did what it meant to. Three traces were
  // shipped before this existed: one where every function shared a code
  // address, one where returning cost no instruction, and one whose header
  // claimed a hundred megabytes for a six-hundred-megabyte file. Each was
  // valid-looking and each was found by the simulator or by hand, days later.
  // Anything the reader will reject, or that is quietly self-inconsistent,
  // should be caught here while the cause is still nearby.
  {
    std::ifstream vin(opt.out, std::ios::binary | std::ios::ate);
    const std::uint64_t bytes = static_cast<std::uint64_t>(vin.tellg());
    const std::uint64_t want = sizeof(nmfc::header) + n_records * sizeof(nmfc::record);
    const auto fail = [&](const std::string& why) {
      vin.close();
      std::remove(opt.out.c_str());
      die("the trace it just wrote is invalid, so it has been deleted -- " + why);
    };
    if (bytes != want) {
      fail("file is " + std::to_string(bytes) + " bytes, header describes " + std::to_string(want));
    }
    vin.seekg(sizeof(nmfc::header));

    std::uint64_t idx = 0, calls_seen = 0, body_seen = 0, expect_body = 0;
    bool in_call = false;
    std::set<std::uint64_t> tokens;
    nmfc::record r{};
    while (vin.read(reinterpret_cast<char*>(&r), sizeof(r))) {
      const auto kind = static_cast<nmfc::op>(r.kind);
      if (r.kind > static_cast<std::uint8_t>(nmfc::op::SPAWN)) {
        fail("record " + std::to_string(idx) + " has kind " + std::to_string(r.kind) + ", which is not an op");
      }
      switch (kind) {
      case nmfc::op::CALL:
        if (in_call) {
          fail("record " + std::to_string(idx) + " opens an invocation while one is still open");
        }
        if (!tokens.insert(r.token).second) {
          fail("token " + std::to_string(r.token) + " is used by two invocations");
        }
        in_call = true;
        expect_body = nmfc::call_body_length(r.aux1);
        body_seen = 0;
        ++calls_seen;
        break;
      case nmfc::op::BODY:
      case nmfc::op::ATOMIC:
      case nmfc::op::SPAWN:
        if (!in_call) {
          fail("record " + std::to_string(idx) + " is a body instruction outside any invocation");
        }
        if (r.instr.ip < code.sim_base || r.instr.ip >= code.sim_base + (code.real_end - code.real_base)) {
          fail("body instruction at " + std::to_string(idx) + " has a program counter outside the code region");
        }
        ++body_seen;
        break;
      case nmfc::op::RET:
        if (!in_call) {
          fail("record " + std::to_string(idx) + " ends an invocation that never began");
        }
        if (body_seen != expect_body) {
          fail("invocation ending at " + std::to_string(idx) + " declared " + std::to_string(expect_body) + " body records but emitted " +
               std::to_string(body_seen));
        }
        if (r.aux0 == 0 || r.aux0 > opt.num_regs) {
          fail("invocation ending at " + std::to_string(idx) + " returns " + std::to_string(r.aux0) + " register words");
        }
        in_call = false;
        break;
      case nmfc::op::HOST:
      case nmfc::op::JOIN:
      case nmfc::op::PAGE_HINT:
        if (in_call) {
          fail("record " + std::to_string(idx) + " interrupts an invocation; bodies must be contiguous");
        }
        break;
      }
      ++idx;
    }
    if (in_call) {
      fail("the stream ends inside an invocation");
    }
    if (idx != n_records) {
      fail("read back " + std::to_string(idx) + " records, wrote " + std::to_string(n_records));
    }
    if (calls_seen != n_calls) {
      fail("read back " + std::to_string(calls_seen) + " invocations, header says " + std::to_string(n_calls));
    }
    std::fprintf(stderr, "[annotate] validated: %lu records, %lu invocations, every body contiguous and inside the code region\n", idx, calls_seen);
  }
  return 0;
}
