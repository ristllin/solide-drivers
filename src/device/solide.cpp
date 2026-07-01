#include "solide/solide.h"

namespace solide {

BeginResult begin() {
  BeginResult r{};
  r.storage = storage::begin();   // mount SD first (memory's JSON half uses it)
  r.memory  = memory::begin();
  r.display = display::begin();
  r.leds    = leds::begin();
  r.input   = input::begin();
  return r;
}

}  // namespace solide
