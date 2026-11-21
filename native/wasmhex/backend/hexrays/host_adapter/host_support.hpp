#pragma once

#include <cstddef>
#include <string>

namespace wasmhex
{

// One byte-exact patch site in a stock hexx64.dylib: host.cpp verifies
// `expected` at `offset` BEFORE writing `replacement` (fail-closed — a
// mismatched binary is never patched, see DECISIONS #15).
struct host_patch_t
{
  std::size_t offset;           // file offset in the stock hexx64.dylib
  unsigned char expected[4];    // bytes that must already be there
  unsigned char replacement[4]; // bytes written once the guard passes
};

// One supported {IDA version x platform x hexx64 build}. `hexx64_sha256` is
// the SHA-256 of the STOCK hexx64.dylib BEFORE patching (lowercase hex).
// `patches`/`patch_count` are the patch set proven against exactly that
// build (never shared between entries — offsets are build-specific).
struct host_support_entry_t
{
  const char *ida_version;     // e.g. "9.5" — the IDA line this hexx64 build ships with
  const char *platform;        // e.g. "macOS arm64"
  const char *hexx64_sha256;   // 64 lowercase hex chars of the stock hexx64.dylib
  const char *notes;           // human note shown in messages/docs
  const host_patch_t *patches; // patch set proven for THIS build
  std::size_t patch_count;     // number of patch sites (>= 1)
};

enum class host_support_status
{
  supported,            // (platform, sha) matches a table entry
  unsupported_platform, // the platform has no table entry at all
  unknown_build,        // platform has entries, but no entry carries this sha
};

// The table, in declaration order.
const host_support_entry_t *host_support_entries();
std::size_t host_support_entry_count();

// Short status name ("supported", "unsupported_platform", "unknown_build") —
// for tests and log lines.
const char *host_support_status_text(host_support_status status);

// Classify one candidate host against the table. `hexx64_sha256` may use
// either case (compared case-insensitively); platform compares as an exact
// string. The IDA version is NOT an input — the sha pins the exact build,
// and the plugin may be compiled against a different SDK line than the IDA
// it runs in (see the header comment).
host_support_status classify_host_support(const char *platform, const char *hexx64_sha256);

// The entry a candidate host matched (nullptr when classify_host_support
// would not return `supported`). host.cpp applies the returned entry's
// patches — the table is the single source of truth for both the SHA gate
// and the byte-exact patch set.
const host_support_entry_t *match_host_support(const char *platform, const char *hexx64_sha256);

// The clear, guided refusal message for a candidate OUTSIDE the table
// (returns the empty string for `supported`). Multi-line, one fact or
// instruction per line, without any "wasmhex: " prefix — callers prefix
// per their logging convention. Never empty for a refusal status.
std::string host_support_message(host_support_status status, const char *platform,
                                 const char *hexx64_sha256);

} // namespace wasmhex
