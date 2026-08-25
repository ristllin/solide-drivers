#pragma once
// ============================================================================
// Nuage Solide S3 - full-card format: portable state machine + result codes
//
// This header is deliberately Arduino-free and FATFS-free so the format
// SEQUENCING and its guards can be host-tested with no hardware. storage.cpp
// supplies the real device steps (f_mkfs, unmount/remount); this file owns only
// the ordering and the refuse-when-absent guard, which is the part that a wrong
// change would silently break.
// ============================================================================
namespace solide::storage {

// Outcome of a full-card format attempt, ordered from success to the exact stage
// that failed so a caller can surface one honest next step.
enum class FormatResult {
  Ok = 0,         // card reformatted and remounted, ready for I/O
  NoCard,         // no card mounted: refused before touching anything
  MkfsFailed,     // f_mkfs returned non-OK (bad/locked card, too small)
  RemountFailed,  // format succeeded but the card would not re-mount after
};

// Stable, machine-oriented tag for logs and callers. Not user-facing copy.
inline const char* formatResultStr(FormatResult r) {
  switch (r) {
    case FormatResult::Ok:            return "ok";
    case FormatResult::NoCard:        return "no-card";
    case FormatResult::MkfsFailed:    return "mkfs-failed";
    case FormatResult::RemountFailed: return "remount-failed";
  }
  return "unknown";
}

namespace detail {

// The portable format state machine, decoupled from FATFS/Arduino for host
// testing. It invokes the injected device steps in a fixed order and owns the
// guards:
//   1. refuse if no card is mounted - mkfs is NEVER invoked in this case;
//   2. run mkfs; propagate its failure and stop (no remount on a failed mkfs);
//   3. remount; propagate its failure;
//   4. success.
// mounted:  is a card currently mounted (storage::available()).
// doMkfs:   run f_mkfs on the active volume; return true on FR_OK.
// remount:  end() then begin() the card; return true if it mounts clean after.
template <typename MkfsFn, typename RemountFn>
FormatResult runFormat(bool mounted, MkfsFn doMkfs, RemountFn remount) {
  if (!mounted) return FormatResult::NoCard;
  if (!doMkfs()) return FormatResult::MkfsFailed;
  if (!remount()) return FormatResult::RemountFailed;
  return FormatResult::Ok;
}

}  // namespace detail
}  // namespace solide::storage
