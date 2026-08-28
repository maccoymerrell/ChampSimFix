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

} // extern "C"

#endif
