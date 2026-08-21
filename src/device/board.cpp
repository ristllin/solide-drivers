#include "solide/board.h"
#include "solide/boards/active_board.h"

// Runtime accessor. The compile-time selection lives in active_board.h
// (activeBoard()); board() just exposes it to non-constexpr callers.
namespace solide {

const Board& board() {
  return activeBoard();
}

}  // namespace solide
