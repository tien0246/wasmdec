// Host support table — data + pure matching. See host_support.hpp.
#include "host_support.hpp"

#include <algorithm>
#include <cstring>

namespace wasmhex
{
namespace
{

// The official support matrix. Do NOT widen without a new verified patch
// table: each entry's SHA-256 pins the exact hexx64 build its patch set was
// proven against. Two entries may share the IDA line (an IDA update moves
// the patch offsets — 261016 -> 261026 shifted all five sites) but never
// the SHA.
// Patch semantics (identical gates in both builds; sites re-derived from the
// binaries themselves — see the evidence):
// 1. b.ne — init-time processor-ID gate #1 in the Hex-Rays module init
// (`required_id != ph->id` destroys the module and refuses the plugin);
// NOP lets the stock hexx64 load under a custom processor module.
// 2. b.ne — processor-ID gate #2: the mismatch path prints "requires %s
// processor (%s cannot handle it)" and can qexit(1); NOP skips it.
// 3. cbz — decompile-entry architecture gate: `!arch_supported(ctx)` returns
// MERR_BADARCH (-31) before the mba is created (Nexus rejects custom
// processor IDs before wasmhex installs its microcode filter); NOP
// falls through to decompilation.
// 4. movn w0,#-13 — the frame-check failure block of the frame builder
// returns MERR_BADFRAME (-13); replaced with `movz w0,#0` (MERR_OK) so
// handmade WASM frames decompile.
// 5. b.ls — internal complexity counter (`[ctx+0x590] > 1`) forces
// MERR_COMPLEX (-22); replaced with `b +0x20` onto the continuation
// block (same structural landing as the proven old-build patch).
constexpr host_patch_t k_patches_hexx64_88d5c6dd[] = {
  { 0x2FF788, { 0xA1, 0x0A, 0x00, 0x54 }, { 0x1F, 0x20, 0x03, 0xD5 } },
  { 0x2FF864, { 0x41, 0x06, 0x00, 0x54 }, { 0x1F, 0x20, 0x03, 0xD5 } },
  { 0x00ED2A4, { 0x80, 0x09, 0x00, 0x34 }, { 0x1F, 0x20, 0x03, 0xD5 } },
  { 0x01C798, { 0x80, 0x01, 0x80, 0x12 }, { 0x00, 0x00, 0x80, 0x52 } },
  { 0x17F170, { 0xA9, 0x00, 0x00, 0x54 }, { 0x08, 0x00, 0x00, 0x14 } },
};
// IDA 9.5 build 261026. Same five
// gates, same expected instruction encodings — only the addresses moved.
constexpr host_patch_t k_patches_hexx64_e813cef8[] = {
  { 0x30C7CC, { 0xA1, 0x0A, 0x00, 0x54 }, { 0x1F, 0x20, 0x03, 0xD5 } },
  { 0x30C8A8, { 0x41, 0x06, 0x00, 0x54 }, { 0x1F, 0x20, 0x03, 0xD5 } },
  { 0x0EFE78, { 0x80, 0x09, 0x00, 0x34 }, { 0x1F, 0x20, 0x03, 0xD5 } },
  { 0x1CAD8, { 0x80, 0x01, 0x80, 0x12 }, { 0x00, 0x00, 0x80, 0x52 } },
  { 0x182F8C, { 0xA9, 0x00, 0x00, 0x54 }, { 0x08, 0x00, 0x00, 0x14 } },
};

const host_support_entry_t k_host_support_table[] = {
  { "9.5", "macOS arm64", "88d5c6ddcbff4d35e5111f48a5d8ffee2ddd3e0da255703277bf05038d063b1b",
    "IDA Professional 9.5, stock hexx64.dylib (SHA-256 measured from the "
    "application bundle)",
    k_patches_hexx64_88d5c6dd,
    sizeof(k_patches_hexx64_88d5c6dd) / sizeof(k_patches_hexx64_88d5c6dd[0]) },
  { "9.5", "macOS arm64", "e813cef8f8785f73507abbfc95ccb1a837c36c84cb0f48494ee1a8d147361211",
    "IDA Professional 9.5 build 261026 (26/10/2026 update), stock hexx64.dylib "
    "— patch sites re-derived from the binary (T0 rev38)",
    k_patches_hexx64_e813cef8,
    sizeof(k_patches_hexx64_e813cef8) / sizeof(k_patches_hexx64_e813cef8[0]) },
};

bool hex_equal_case_insensitive(const char *a, const char *b)
{
  if ( a == nullptr || b == nullptr )
    return false;
  if ( std::strlen(a) != std::strlen(b) )
    return false;
  for ( const char *pa = a, *pb = b; *pa != '\0'; ++pa, ++pb )
  {
    const int ca = *pa >= 'A' && *pa <= 'F' ? *pa - 'A' + 'a' : *pa;
    const int cb = *pb >= 'A' && *pb <= 'F' ? *pb - 'A' + 'a' : *pb;
    if ( ca != cb )
      return false;
  }
  return true;
}

const host_support_entry_t *first_entry_for_platform(const char *platform)
{
  const host_support_entry_t *const table = host_support_entries();
  const std::size_t count = host_support_entry_count();
  const auto end = table + count;
  const auto found = std::find_if(table, end, [platform](const host_support_entry_t &entry)
                                  { return std::strcmp(entry.platform, platform) == 0; });
  return found == end ? nullptr : found;
}

void append_sha_build_line(const host_support_entry_t &entry, std::string *out)
{
  out->append("  expected the IDA ");
  out->append(entry.ida_version);
  out->append(" ");
  out->append(entry.platform);
  out->append(" build, SHA-256 ");
  out->append(entry.hexx64_sha256);
  out->append("\n");
}

void append_all_sha_build_lines(const char *platform, std::string *out)
{
  // One line per supported build on this platform — the table may pin
  // several builds of the same IDA line (an IDA update changes the SHA).
  const host_support_entry_t *const table = host_support_entries();
  const std::size_t count = host_support_entry_count();
  for ( std::size_t i = 0; i < count; ++i )
    if ( std::strcmp(table[i].platform, platform) == 0 )
      append_sha_build_line(table[i], out);
}

void append_supported_platforms(std::string *out)
{
  const host_support_entry_t *const table = host_support_entries();
  const std::size_t count = host_support_entry_count();
  bool first = true;
  for ( std::size_t i = 0; i < count; ++i )
  {
    bool seen = false;
    for ( std::size_t j = 0; j < i; ++j )
      seen |= std::strcmp(table[j].platform, table[i].platform) == 0;
    if ( seen )
      continue;
    if ( !first )
      out->append(", ");
    out->append(table[i].platform);
    first = false;
  }
}

} // namespace

const host_support_entry_t *host_support_entries()
{
  return k_host_support_table;
}

std::size_t host_support_entry_count()
{
  return sizeof(k_host_support_table) / sizeof(k_host_support_table[0]);
}

const char *host_support_status_text(host_support_status status)
{
  switch ( status )
  {
    case host_support_status::supported: return "supported";
    case host_support_status::unsupported_platform: return "unsupported_platform";
    case host_support_status::unknown_build: return "unknown_build";
  }
  return "invalid";
}

host_support_status classify_host_support(const char *platform, const char *hexx64_sha256)
{
  if ( platform == nullptr || hexx64_sha256 == nullptr )
    return host_support_status::unsupported_platform;
  const host_support_entry_t *const entry = first_entry_for_platform(platform);
  if ( entry == nullptr )
    return host_support_status::unsupported_platform;
  const host_support_entry_t *const table = host_support_entries();
  const std::size_t count = host_support_entry_count();
  for ( std::size_t i = 0; i < count; ++i )
  {
    if ( std::strcmp(table[i].platform, platform) != 0 )
      continue;
    if ( hex_equal_case_insensitive(table[i].hexx64_sha256, hexx64_sha256) )
      return host_support_status::supported;
  }
  return host_support_status::unknown_build;
}

const host_support_entry_t *match_host_support(const char *platform, const char *hexx64_sha256)
{
  if ( platform == nullptr || hexx64_sha256 == nullptr )
    return nullptr;
  const host_support_entry_t *const table = host_support_entries();
  const std::size_t count = host_support_entry_count();
  for ( std::size_t i = 0; i < count; ++i )
  {
    if ( std::strcmp(table[i].platform, platform) != 0 )
      continue;
    if ( hex_equal_case_insensitive(table[i].hexx64_sha256, hexx64_sha256) )
      return &table[i];
  }
  return nullptr;
}

std::string host_support_message(host_support_status status, const char *platform,
                                 const char *hexx64_sha256)
{
  if ( status == host_support_status::supported )
    return std::string();

  std::string out;
  switch ( status )
  {
    case host_support_status::unsupported_platform:
      out.append("this host is outside the wasmhex Hex-Rays support table: platform '");
      out.append(platform == nullptr ? "?" : platform);
      out.append("' has no supported hexx64 build.\n");
      out.append("supported platforms: ");
      append_supported_platforms(&out);
      out.append(".\n");
      break;
    case host_support_status::unknown_build:
    {
      const host_support_entry_t *entry
        = first_entry_for_platform(platform == nullptr ? "" : platform);
      out.append("hexx64.dylib does not match any build in the wasmhex support table —\n"
                 "IDA was likely updated or patched.\n");
      if ( entry != nullptr )
        append_all_sha_build_lines(platform == nullptr ? "" : platform, &out);
      out.append("  actual   SHA-256: ");
      out.append(hexx64_sha256 == nullptr ? "?" : hexx64_sha256);
      out.append("\n");
      out.append("wasmhex needs a new verified patch table for that build; the Hex-Rays\n"
                 "backend stays disabled until then.\n");
      break;
    }
    case host_support_status::supported: break;
  }
  out.append("100% compatibility with every IDA build is impossible while wasmhex patches\n"
             "the stock binary; the official Hex-Rays SDK API is the road to universal\n"
             "support (host_support.hpp). Disassembly keeps working — only F5 is unavailable.");
  return out;
}

} // namespace wasmhex
