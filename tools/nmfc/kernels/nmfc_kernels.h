/**
 * Function bodies, written as real C++.
 *
 * Nothing here invents an instruction or a program counter. Each function is
 * compiled by the toolchain into its own symbol at its own address, and the
 * annotation pass afterwards reads the addresses the linker assigned. That is
 * the whole point of this file existing: the previous generator emitted
 * instruction streams by hand, and every stream it emitted was wrong in the
 * same way -- one code address for every function in the workload.
 *
 * The attributes are load-bearing, not decoration:
 *   noinline  the body must survive as a call, or there is no invocation to see
 *   noipa     stops interprocedural analysis rewriting the signature or body
 *   noclone   stops specialised copies appearing at other addresses
 *   used      stops removal when a caller looks dead
 *   aligned   the entry lands on a line boundary, so a function never starts
 *             half way through a block by accident
 *
 * Two more live in the build flags because they cannot be spelled here:
 * -fno-ipa-icf, without which identical-code folding merges two functions that
 * happen to compile the same into one symbol -- the exact failure the previous
 * generator had by hand -- and -fno-optimize-sibling-calls, without which the
 * return becomes a tail jump and stops being an instruction.
 */
#ifndef NMFC_KERNELS_H
#define NMFC_KERNELS_H

#include <cstdint>

#define NMFC_FUNCTION __attribute__((noinline, noipa, noclone, used, aligned(64)))

extern "C" {

/** Bracket the region of interest; the Pin tracer finds these by name. */
__attribute__((noinline, used)) void __champsim_start_trace(void);
__attribute__((noinline, used)) void __champsim_stop_trace(void);

/**
 * Block until the invocation that produced `p` has committed it.
 *
 * The memory-committing loop's wait site. A caller reading an invocation's
 * output block must call this first, and the annotation pass turns the load it
 * performs into the join for whichever invocation wrote that address.
 *
 * It has to *touch* the address rather than merely take it: a trace records
 * the addresses an instruction accessed, not the values in its registers, so a
 * marker that only received a pointer would leave nothing behind to resolve
 * against. The load is volatile so it survives optimisation, and the function
 * is noinline so the wait has one identifiable program counter.
 */
__attribute__((noinline, used)) void __nmfc_wait(const void* p);

} // extern "C"

/**
 * "I commit work here": the function declares the address its result lives at.
 *
 * Ownership is by address, so the commit has to name one, and it has to be
 * marked rather than inferred from whichever store happened to come last -- a
 * function may write scratch it never publishes.
 *
 * It cannot be a called hook the way __nmfc_wait is. A call from inside an
 * offloaded function pushes a return address, and a function core has a
 * register file and no stack; it would also take the program counter outside
 * the function's range and split the body in two. So the site is marked in
 * place with a two-byte no-op the disassembly can find, immediately before the
 * store that publishes the block.
 */
#define NMFC_COMMIT(p, v)                                                                                                                            \
  do {                                                                                                                                              \
    asm volatile("nopl 0x2a(%%rax)" : : : "memory");                                                                                                  \
    *(p) = (v);                                                                                                                                     \
  } while (0)

extern "C" {

} // extern "C"

#endif
