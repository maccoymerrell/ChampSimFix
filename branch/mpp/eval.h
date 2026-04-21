#ifndef _EVAL_H
#define _EVAL_H
double eval (int, unsigned long long int *);

#define MAX_BMARKS	3000

#ifndef NSPECS
#define NSPECS	1
#endif

#define MAX_TABLES	128

#ifndef NUM_TABLES
#define NUM_TABLES	64
#endif

#ifndef NTHREADS
#define NTHREADS	48
#endif

#define LG_COEFF_MAG	2

void read_traces (void);

typedef enum {
  OPTYPE_OP               =2,

  OPTYPE_RET_UNCOND,
  OPTYPE_JMP_DIRECT_UNCOND,
  OPTYPE_JMP_INDIRECT_UNCOND,
  OPTYPE_CALL_DIRECT_UNCOND,
  OPTYPE_CALL_INDIRECT_UNCOND,

  OPTYPE_RET_COND,
  OPTYPE_JMP_DIRECT_COND,
  OPTYPE_JMP_INDIRECT_COND,
  OPTYPE_CALL_DIRECT_COND,
  OPTYPE_CALL_INDIRECT_COND,

  OPTYPE_ERROR,

  OPTYPE_MAX
} OpType;

#ifdef __APPLE__
#define MONKIPATH    "/Users/djimenez/monkip32/"
#else
#define MONKIPATH    "/home/djimenez/monkip32"
//#define MONKIPATH    "/home/faculty/d/djimenez/monkip32"
#endif
#endif
