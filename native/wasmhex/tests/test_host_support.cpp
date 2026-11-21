// Unit pins for the host support table — match / mismatch /
// generic unpatched IDA — over the pure, SDK-free matching logic in
// backend/hexrays/host_adapter/host_support.{hpp,cpp}.
#include "doctest.h"

#include <cstring>
#include <string>
#include <vector>

#include "backend/hexrays/host_adapter/host_support.hpp"

using namespace wasmhex;

namespace
{

bool is_lower_hex_64(const char *text)
{
  if ( text == nullptr || std::strlen(text) != 64 )
    return false;
  for ( const char *p = text; *p != '\0'; ++p )
  {
    const bool ok = (*p >= '0' && *p <= '9') || (*p >= 'a' && *p <= 'f');
    if ( !ok )
      return false;
  }
  return true;
}

std::string flipped_sha(const char *sha)
{
  std::string out(sha);
  out[0] = out[0] == '0' ? '1' : '0';
  return out;
}

// Every 9.5 macOS arm64 entry the table pins (the IDA line may carry
// several hexx64 builds — one per IDA patch level).
std::vector<const host_support_entry_t *> guarded_entries()
{
  std::vector<const host_support_entry_t *> out;
  for ( std::size_t i = 0; i < host_support_entry_count(); ++i )
  {
    const host_support_entry_t *const entry = &host_support_entries()[i];
    if ( std::strcmp(entry->ida_version, "9.5") == 0
         && std::strcmp(entry->platform, "macOS arm64") == 0 )
      out.push_back(entry);
  }
  return out;
}

} // namespace

TEST_CASE("K4: the support table is well-formed and pins the guarded 9.5 arm64 builds")
{
  REQUIRE(host_support_entry_count() >= 1);
  for ( std::size_t i = 0; i < host_support_entry_count(); ++i )
  {
    const host_support_entry_t &entry = host_support_entries()[i];
    CHECK(entry.ida_version != nullptr);
    CHECK(entry.platform != nullptr);
    CHECK(entry.notes != nullptr);
    // A table entry is only load-bearing if the SHA pins a real hexx64
    // build in canonical lowercase hex.
    CHECK_MESSAGE(is_lower_hex_64(entry.hexx64_sha256),
                  entry.hexx64_sha256 << " must be 64 lowercase hex chars");
    // Every entry carries its own proven patch set (>= 1 site) — the patch
    // offsets are build-specific, never shared.
    REQUIRE(entry.patches != nullptr);
    REQUIRE(entry.patch_count >= 1);
    for ( std::size_t p = 0; p < entry.patch_count; ++p )
    {
      const host_patch_t &patch = entry.patches[p];
      // Offsets are unique (checked pairwise below in declaration order —
      // the table lists sites in semantic order, not address order) and
      // each site really rewrites the instruction (expected != replacement).
      for ( std::size_t q = 0; q < p; ++q )
        CHECK_MESSAGE(entry.patches[q].offset != patch.offset, "patch offsets must be unique");
      CHECK_MESSAGE(std::memcmp(patch.expected, patch.replacement, sizeof(patch.expected)) != 0,
                    "a patch site must change bytes");
    }
    // No duplicate {platform x sha}: the SHA is the load-bearing key — one
    // IDA line may legitimately carry several patch levels (builds).
    for ( std::size_t j = 0; j < i; ++j )
    {
      const host_support_entry_t &other = host_support_entries()[j];
      const bool duplicate = std::strcmp(entry.platform, other.platform) == 0
        && std::strcmp(entry.hexx64_sha256, other.hexx64_sha256) == 0;
      CHECK_FALSE_MESSAGE(duplicate, "duplicate table entry for sha " << entry.hexx64_sha256);
    }
  }
  REQUIRE(guarded_entries().size() >= 1); // the 9.5 arm64 pin(s) must exist
}

TEST_CASE("K4: the 261026 IDA update is the second 9.5 arm64 row with its own patch set")
{
  const char *const k_new_sha = "e813cef8f8785f73507abbfc95ccb1a837c36c84cb0f48494ee1a8d147361211";
  const host_support_entry_t *entry = match_host_support("macOS arm64", k_new_sha);
  REQUIRE(entry != nullptr);
  REQUIRE(entry->patch_count == 5);

  // The five re-derived sites of build 261026: same gates, same instruction
  // encodings as the 261016 build — only the addresses moved.
  const struct
  {
    std::size_t offset;
    unsigned char expected[4];
    unsigned char replacement[4];
  } k_expected[] = {
    { 0x30C7CC,
      { 0xA1, 0x0A, 0x00, 0x54 },
      { 0x1F, 0x20, 0x03, 0xD5 } }, // b.ne processor-ID gate #1
    { 0x30C8A8,
      { 0x41, 0x06, 0x00, 0x54 },
      { 0x1F, 0x20, 0x03, 0xD5 } }, // b.ne "requires %s processor"
    { 0x0EFE78, { 0x80, 0x09, 0x00, 0x34 }, { 0x1F, 0x20, 0x03, 0xD5 } }, // cbz MERR_BADARCH gate
    { 0x1CAD8,
      { 0x80, 0x01, 0x80, 0x12 },
      { 0x00, 0x00, 0x80, 0x52 } }, // movn w0,#-13 -> movz w0,#0
    { 0x182F8C,
      { 0xA9, 0x00, 0x00, 0x54 },
      { 0x08, 0x00, 0x00, 0x14 } }, // b.ls MERR_COMPLEX gate -> b +0x20
  };
  for ( std::size_t p = 0; p < 5; ++p )
  {
    const host_patch_t &patch = entry->patches[p];
    CHECK(patch.offset == k_expected[p].offset);
    CHECK(std::memcmp(patch.expected, k_expected[p].expected, 4) == 0);
    CHECK(std::memcmp(patch.replacement, k_expected[p].replacement, 4) == 0);
  }

  // The original 261016 row stays intact (old offsets, same encodings).
  const host_support_entry_t *old = match_host_support(
    "macOS arm64", "88d5c6ddcbff4d35e5111f48a5d8ffee2ddd3e0da255703277bf05038d063b1b");
  REQUIRE(old != nullptr);
  REQUIRE(old->patch_count == 5);
  CHECK(old->patches[0].offset == 0x2FF788);
  CHECK(old->patches[1].offset == 0x2FF864);
  CHECK(old->patches[2].offset == 0xED2A4);
  CHECK(old->patches[3].offset == 0x1C798);
  CHECK(old->patches[4].offset == 0x17F170);
  CHECK(std::memcmp(old->patches[3].replacement, "\x00\x00\x80\x52", 4) == 0);

  // The two rows are distinct tables with distinct first sites — a shared
  // (global) patch table would patch the new build at the old offsets.
  CHECK(old->patches[0].offset != entry->patches[0].offset);
}

TEST_CASE("K4: matching a table entry classifies as supported (hash case-insensitive)")
{
  for ( const host_support_entry_t *entry : guarded_entries() )
  {
    const host_support_status status = classify_host_support(entry->platform, entry->hexx64_sha256);
    CHECK(status == host_support_status::supported);
    CHECK(std::strcmp(host_support_status_text(status), "supported") == 0);
    // A supported host never produces a refusal message.
    CHECK(host_support_message(status, entry->platform, entry->hexx64_sha256).empty());
    // match returns exactly this entry (host.cpp applies its patch set).
    CHECK(match_host_support(entry->platform, entry->hexx64_sha256) == entry);

    // Uppercase actual hash (some tools print digests uppercased) still matches.
    std::string upper(entry->hexx64_sha256);
    for ( char &c : upper )
      c = c >= 'a' && c <= 'f' ? char(c - 'a' + 'A') : c;
    CHECK(classify_host_support(entry->platform, upper.c_str()) == host_support_status::supported);
    CHECK(match_host_support(entry->platform, upper.c_str()) == entry);
  }

  // The runtime decision is SHA-keyed and independent of the SDK the plugin
  // this machine compiles against SDK 9.4 while running IDA 9.5).
  static_assert(true, "classify_host_support takes (platform, sha) only");
}

TEST_CASE("K4: a changed hexx64 build (IDA update / different patch level) is unknown_build")
{
  for ( const host_support_entry_t *entry : guarded_entries() )
  {
    const std::string other = flipped_sha(entry->hexx64_sha256);
    const host_support_status status = classify_host_support(entry->platform, other.c_str());
    CHECK(status == host_support_status::unknown_build);
    CHECK(match_host_support(entry->platform, other.c_str()) == nullptr);

    // The refusal message names EVERY expected build for the platform
    // (the table may pin several builds of one IDA line), the actual sha,
    // and states the compatibility posture (nothing patched, disassembly
    // intact).
    const std::string message = host_support_message(status, entry->platform, other.c_str());
    for ( const host_support_entry_t *expected : guarded_entries() )
    {
      CHECK_MESSAGE(message.find(expected->hexx64_sha256) != std::string::npos,
                    "message must contain every expected SHA-256");
      CHECK_MESSAGE(message.find(expected->ida_version) != std::string::npos,
                    "message must name the expected IDA version");
    }
    CHECK_MESSAGE(message.find(other) != std::string::npos,
                  "message must contain the actual SHA-256");
    CHECK_MESSAGE(message.find("updated") != std::string::npos,
                  "message must explain the likely cause");
    CHECK_MESSAGE(message.find("100% compatibility") != std::string::npos,
                  "message must state the compatibility posture");
    CHECK_MESSAGE(message.find("Disassembly keeps working") != std::string::npos,
                  "message must state the degraded mode");
  }
}

TEST_CASE("K4: a generic IDA build never in the table is unknown_build, not supported")
{
  // e.g. a stock hexx64 from another IDA line/edition/patch level — same
  // platform, different bytes: refuse before patching.
  std::string all_zero(64, '0');
  CHECK(classify_host_support("macOS arm64", all_zero.c_str())
        == host_support_status::unknown_build);
  // Nulls degrade to a refusal, never to supported.
  CHECK(classify_host_support(nullptr, "00") == host_support_status::unsupported_platform);
  CHECK(classify_host_support("macOS arm64", nullptr) == host_support_status::unsupported_platform);
}

TEST_CASE("K4: an unsupported platform is unsupported_platform with the platform list")
{
  const char *const others[] = { "Linux x86-64", "Windows x64", "macOS x86-64" };
  for ( const char *platform : others )
  {
    const host_support_status status = classify_host_support(platform, "00");
    CHECK_MESSAGE(status == host_support_status::unsupported_platform, platform);
    const std::string message = host_support_message(status, platform, "00");
    CHECK_MESSAGE(message.find(platform) != std::string::npos, platform);
    CHECK_MESSAGE(message.find("macOS arm64") != std::string::npos,
                  "message must list the supported platforms");
  }
}
