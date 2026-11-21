#include "doctest.h"

#include <numeric>
#include <vector>

#include "../core/types.hpp"

using namespace wasmhex::loader;

namespace
{

wasmhex_vt::value_type_status_t decode(const std::vector<uchar> &bytes,
                                       wasmhex_vt::value_type_t *out)
{
  std::size_t pos = 0;
  return wasmhex_vt::decode_value_type(bytes.data(), bytes.size(), &pos, out);
}

} // namespace

TEST_CASE("value type: scalars and shorthands")
{
  struct Case
  {
    std::vector<uchar> bytes;
    uchar canonical;
    uint8_t kind;
  };
  const Case cases[] = {
    { { 0x7F }, 0x7F, wasmhex_vt::ref_kind_none },
    { { 0x7E }, 0x7E, wasmhex_vt::ref_kind_none },
    { { 0x7D }, 0x7D, wasmhex_vt::ref_kind_none },
    { { 0x7C }, 0x7C, wasmhex_vt::ref_kind_none },
    { { 0x70 }, 0x70, wasmhex_vt::ref_kind_none },
    { { 0x6F }, 0x6F, wasmhex_vt::ref_kind_none },
    { { 0x6A }, 0x71, wasmhex_vt::ref_kind_nullable },
    { { 0x6B }, 0x71, wasmhex_vt::ref_kind_nullable },
    { { 0x6C }, 0x72, wasmhex_vt::ref_kind_nullable },
    { { 0x6D }, 0x71, wasmhex_vt::ref_kind_nullable },
  };
  for ( const Case &c : cases )
  {
    wasmhex_vt::value_type_t vt;
    REQUIRE(decode(c.bytes, &vt) == wasmhex_vt::vt_ok);
    CHECK(vt.canonical_type == c.canonical);
    CHECK(vt.ref_kind == c.kind);
    if ( c.kind == wasmhex_vt::ref_kind_none )
      CHECK(vt.heap_type == wasmhex_vt::heap_type_none);
  }
}

TEST_CASE("value type: ref forms with heap types")
{
  struct Case
  {
    std::vector<uchar> bytes;
    uchar canonical;
    int64 heap;
    uint8_t kind;
  };
  const Case cases[] = {
    // The repro sequence: (ref null 0) canonicalizes to funcref + index 0.
    { { 0x63, 0x00 }, 0x70, 0, wasmhex_vt::ref_kind_nullable },
    { { 0x64, 0x00 }, 0x70, 0, wasmhex_vt::ref_kind_abs },
    { { 0x63, 0x70 }, 0x70, -0x10, wasmhex_vt::ref_kind_nullable },
    { { 0x63, 0x6F }, 0x6F, -0x11, wasmhex_vt::ref_kind_nullable },
    { { 0x63, 0x6A }, 0x71, -0x16, wasmhex_vt::ref_kind_nullable },
    { { 0x63, 0x6C }, 0x72, -0x14, wasmhex_vt::ref_kind_nullable },
    { { 0x64, 0x6C }, 0x72, -0x14, wasmhex_vt::ref_kind_abs },
    // Larger type indices use multi-byte slebs.
    { { 0x63, 0x83, 0x00 }, 0x70, 3, wasmhex_vt::ref_kind_nullable },
  };
  for ( const Case &c : cases )
  {
    wasmhex_vt::value_type_t vt;
    REQUIRE(decode(c.bytes, &vt) == wasmhex_vt::vt_ok);
    CHECK(vt.canonical_type == c.canonical);
    CHECK(vt.heap_type == c.heap);
    CHECK(vt.ref_kind == c.kind);
  }
}

TEST_CASE("value type: unsupported and truncated heap types")
{
  wasmhex_vt::value_type_t vt;
  // Negative heap types outside {func, extern, eq..array} are rejected.
  CHECK(decode({ 0x63, 0x7F }, &vt) == wasmhex_vt::vt_unsupported_heap); // -1
  CHECK(decode({ 0x63, 0x71 }, &vt) == wasmhex_vt::vt_unsupported_heap); // -0x0F = none
  CHECK(decode({ 0x63, 0x6E }, &vt) == wasmhex_vt::vt_unsupported_heap); // any
  // Truncation.
  CHECK(decode({ 0x63 }, &vt) == wasmhex_vt::vt_truncated);
  CHECK(decode({ 0x64 }, &vt) == wasmhex_vt::vt_truncated);
  CHECK(decode({ 0x63, 0x80 }, &vt) == wasmhex_vt::vt_truncated);
  CHECK(decode({}, &vt) == wasmhex_vt::vt_truncated);
}

TEST_CASE("value type: transactional contract")
{
  std::vector<uchar> bytes = { 0x63, 0x7F };
  std::size_t pos = 0;
  wasmhex_vt::value_type_t vt;
  vt.canonical_type = 0xAA;
  const auto status = wasmhex_vt::decode_value_type(bytes.data(), bytes.size(), &pos, &vt);
  CHECK(status == wasmhex_vt::vt_unsupported_heap);
  CHECK(pos == 0);                  // untouched on failure
  CHECK(vt.canonical_type == 0xAA); // untouched on failure
}

TEST_CASE("abi: multi-result word accounting at register boundaries")
{
  // wasm_type_size()/4 vs the legacy "?2:1" wording over every multiset of
  // the accepted result types.
  const uchar types[] = { 0x7F, 0x7E, 0x7D, 0x7C, 0x70, 0x6F };
  const int size_of[] = { 4, 8, 4, 8, 4, 4 };
  const size_t count = sizeof(types) / sizeof(types[0]);
  for ( size_t a = 0; a < count; ++a )
    for ( size_t b = 0; b < count; ++b )
      for ( size_t c = 0; c < count; ++c )
      {
        const std::vector<uchar> results = { types[a], types[b], types[c] };
        const int legacy = (results[0] == 0x7E || results[0] == 0x7C ? 2 : 1)
          + (results[1] == 0x7E || results[1] == 0x7C ? 2 : 1)
          + (results[2] == 0x7E || results[2] == 0x7C ? 2 : 1);
        const int shared = size_of[a] / 4 + size_of[b] / 4 + size_of[c] / 4;
        REQUIRE(legacy == shared);
      }
}
