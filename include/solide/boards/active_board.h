#pragma once
#include "solide/board.h"

// ============================================================================
// Compile-time board selection.
//
// The build passes -DSOLIDE_BOARD=<id> (a bare token, e.g. solide_s3). This
// header maps that token to exactly one Board constant and exposes it as the
// constexpr activeBoard(), which every driver reads for its pins instead of
// naming a board constant directly. Adding a board = add a SOLIDE_BID_<id> line,
// an #elif arm, and a boards/board_<id>.h. An unknown id is a hard #error, not a
// silent fallback, so a typo can never quietly ship the wrong pin map.
// ============================================================================

#ifndef SOLIDE_BOARD
#define SOLIDE_BOARD solide_s3
#endif

// id -> small integer (must be unique and non-zero).
#define SOLIDE_BID_solide_s3   1
#define SOLIDE_BID_freenove_s3 2

#define SOLIDE_BID_PASTE(x) SOLIDE_BID_##x
#define SOLIDE_BID_EVAL(x)  SOLIDE_BID_PASTE(x)
#define SOLIDE_ACTIVE_BID   SOLIDE_BID_EVAL(SOLIDE_BOARD)

#if SOLIDE_ACTIVE_BID == SOLIDE_BID_solide_s3
  #include "solide/boards/board_solide_s3.h"
  #define SOLIDE_ACTIVE_BOARD_CONST ::solide::kBoardSolideS3
#elif SOLIDE_ACTIVE_BID == SOLIDE_BID_freenove_s3
  #include "solide/boards/board_freenove_s3.h"
  #define SOLIDE_ACTIVE_BOARD_CONST ::solide::kBoardFreenoveS3
#else
  #error "Unknown SOLIDE_BOARD (expected solide_s3 or freenove_s3)"
#endif

namespace solide {
// constexpr so drivers can bind pins in static initializers.
inline constexpr const Board& activeBoard() { return SOLIDE_ACTIVE_BOARD_CONST; }
}  // namespace solide
