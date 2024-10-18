/*

This code implements a hashed perceptron branch predictor using geometric
history lengths and dynamic threshold setting.  It was written by Daniel
A. Jiménez in March 2019.


The original perceptron branch predictor is from Jiménez and Lin, "Dynamic
Branch Prediction with Perceptrons," HPCA 2001.

The idea of using multiple independently indexed tables of perceptron weights
is from Jiménez, "Fast Path-Based Neural Branch Prediction," MICRO 2003 and
later expanded in "Piecewise Linear Branch Prediction" from ISCA 2005.

The idea of using hashes of branch history to reduce the number of independent
tables is documented in three contemporaneous papers:

1. Seznec, "Revisiting the Perceptron Predictor," IRISA technical report, 2004.

2. Tarjan and Skadron, "Revisiting the Perceptron Predictor Again," UVA
technical report, 2004, expanded and published in ACM TACO 2005 as "Merging
path and gshare indexing in perceptron branch prediction"; introduces the term
"hashed perceptron."

3. Loh and Jiménez, "Reducing the Power and Complexity of Path-Based Neural
Branch Prediction," WCED 2005.

The ideas of using "geometric history lengths" i.e. hashing into tables with
histories of exponentially increasing length, as well as dynamically adjusting
the theta parameter, are from Seznec, "The O-GEHL Branch Predictor," from CBP
2004, expanded later as "Analysis of the O-GEometric History Length Branch
Predictor" in ISCA 2005.

This code uses these ideas, but prefers simplicity over absolute accuracy (I
wrote it in about an hour and later spent more time on this comment block than
I did on the code). These papers and subsequent papers by Jiménez and other
authors significantly improve the accuracy of perceptron-based predictors but
involve tricks and analysis beyond the needs of a tool like ChampSim that
targets cache optimizations. If you want accuracy at any cost, see the winners
of the latest branch prediction contest, CBP 2016 as of this writing, but
prepare to have your face melted off by the complexity of the code you find
there. If you are a student being asked to code a good branch predictor for
your computer architecture class, don't copy this code; there are much better
sources for you to plagiarize.

*/

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vector>

#include "modules.h"
#include "msl/bits.h"
#include "msl/fwcounter.h"

// this many tables

#define NTABLES 16

// maximum history length

#define MAXHIST 232

// minimum history length (for table 1; table 0 is biases)

#define MINHIST 3

// speed for dynamic threshold setting

#define SPEED 18

// 12-bit indices for the tables

#define LOG_TABLE_SIZE 12
#define TABLE_SIZE (1 << LOG_TABLE_SIZE)

// this many 12-bit words will be kept in the global history

#define NGHIST_WORDS (MAXHIST / LOG_TABLE_SIZE + 1)


class hp_new : champsim::modules::branch_predictor
{
  static inline constexpr int history_lengths[NTABLES] = {0, 3, 4, 6, 8, 10, 14, 19, 26, 36, 49, 67, 91, 125, 170, MAXHIST};

  // tables of 8-bit weights
  int tables[NTABLES][TABLE_SIZE];
  //std::vector<std::vector<int>> tables{NTABLES,std::vector<int>{TABLE_SIZE,0}};

  // words that store the global history
  //std::vector<unsigned int> ghist_words{NGHIST_WORDS,0};
  unsigned int ghist_words[NGHIST_WORDS];

  // remember the indices into the tables from prediction to update
  uint64_t indices[NTABLES];
  //std::vector<uint64_t> indices{NTABLES,0};

  // initialize theta to something reasonable,
  int theta = 10;
  int tc;
  int yout;

  public:
    using branch_predictor::branch_predictor;
    bool predict_branch(champsim::address pc);
    void last_branch_result(champsim::address pc, champsim::address branch_target, bool taken, uint8_t branch_type);
    void adjust_threshold(bool correct);
    void initialize_branch_predictor();
};