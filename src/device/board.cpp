#include "solide/board.h"
#include "solide/boards/board_solide_s3.h"

// Compile-time board selection. Add variants here as new Board constants keyed
// off -DSOLIDE_BOARD. Today there is one board.
namespace solide {

const Board& board() {
  return kBoardSolideS3;
}

}  // namespace solide
