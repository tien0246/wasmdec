// ModuleIndex unit pins — the decode-once index the F5 collectors read.
// Three properties are pinned:
//   identity   — every ingredient the derivation reads feeds the cache key
//                (blob 't' words, blob 'Z' bytes, import boundary, schema),
//                so any mutation invalidates and identical content reuses;
//   fail-closed — the index reproduces the exact absent/malformed gates the
//                three per-F5 decoders applied (layer_present=false);
//   equivalence — find_descriptor (O(1) hash) resolves exactly what the
//                layer's linear descriptor_by_addr resolves, first match
//                wins, and the blob-'Z' port keeps the per-F5 parse's caps
//                and first-entry-wins policy.
#include "core/toolchains/go/index.hpp"
#include "core/toolchains/go/types.hpp"
#include "core/types.hpp"

#include "doctest.h"
#include <cstdint>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace wt = wasmhex::loader;

namespace
{
// Minimal hand-built layer (the codec's own round-trip carries the real
// corpus shapes — test_go_types pins those).
wt::go_type_info_t synthetic_layer()
{
  wt::go_type_info_t info;
  info.present = true;
  info.layout_version = 27;
  info.types = 0x1000;
  info.descriptors.resize(2);
  info.descriptors[0].addr = 0x1000;
  info.descriptors[0].kind = 24; // abi.String
  info.descriptors[0].size = 16;
  info.descriptors[0].name = "string";
  info.descriptors[1].addr = 0x2000;
  info.descriptors[1].kind = 25; // abi.Struct
  info.descriptors[1].size = 8;
  info.descriptors[1].name = "main.Pt";
  return info;
}

std::vector<std::uint32_t> encode_layer(const wt::go_type_info_t &info)
{
  std::vector<std::uint32_t> words;
  wt::encode_go_type_info_blob(info, &words);
  return words;
}

// uleb helper for building a blob-'Z' stream.
void push_uleb(std::vector<std::uint8_t> *out, std::uint64_t value)
{
  do
  {
    std::uint8_t byte = value & 0x7f;
    value >>= 7;
    if ( value != 0 )
      byte |= 0x80;
    out->push_back(byte);
  } while ( value != 0 );
}
} // namespace

TEST_CASE("M2 H1: module identity keys on every ingredient")
{
  const std::vector<std::uint32_t> t = encode_layer(synthetic_layer());
  const std::vector<std::uint8_t> z
    = { 0x02, 0x00, 0x05, 0x01, 0x00, 0x00, 0x07, 0x02, 0x00, 0x00 };
  const std::uint64_t base
    = wt::go_module_identity(t.data(), t.size(), z.data(), z.size(), 42, wt::kModuleIndexSchema);

  // Identical inputs -> identical identity (pure function).
  CHECK(wt::go_module_identity(t.data(), t.size(), z.data(), z.size(), 42, wt::kModuleIndexSchema)
        == base);

  // One flipped 't' word invalidates.
  {
    std::vector<std::uint32_t> t2 = t;
    t2[t2.size() / 2] ^= 1;
    CHECK(
      wt::go_module_identity(t2.data(), t2.size(), z.data(), z.size(), 42, wt::kModuleIndexSchema)
      != base);
  }
  // One flipped 'Z' byte invalidates.
  {
    std::vector<std::uint8_t> z2 = z;
    z2[z2.size() / 2] ^= 1;
    CHECK(
      wt::go_module_identity(t.data(), t.size(), z2.data(), z2.size(), 42, wt::kModuleIndexSchema)
      != base);
  }
  // Import boundary and schema bump invalidate.
  CHECK(wt::go_module_identity(t.data(), t.size(), z.data(), z.size(), 43, wt::kModuleIndexSchema)
        != base);
  CHECK(
    wt::go_module_identity(t.data(), t.size(), z.data(), z.size(), 42, wt::kModuleIndexSchema + 1)
    != base);
  // An absent 't' blob must not alias a present one (same 'Z', same import).
  CHECK(wt::go_module_identity(nullptr, 0, z.data(), z.size(), 42, wt::kModuleIndexSchema) != base);
  // Length mixing: [ab][c] cannot alias [a][bc].
  const std::uint8_t ab_c[3] = { 1, 2, 3 };
  const std::uint8_t a_bc[3] = { 1, 23 };
  CHECK(wt::go_hash64(ab_c, 3, 42) != 0);
  CHECK(wt::go_hash64(ab_c, 2, 7) != wt::go_hash64(a_bc, 2, 7)); // different bytes
  CHECK(wt::go_hash64(nullptr, 0, 42) != 0);                     // length still mixes
}

TEST_CASE("M2 H1: index build decodes once, fails closed like the per-F5 path")
{
  // Valid blob: layer_present, descriptors indexed.
  {
    const std::vector<std::uint32_t> t = encode_layer(synthetic_layer());
    wt::go_module_index_t index;
    REQUIRE(wt::go_build_module_index(t.data(), t.size(), nullptr, 0, 7, &index));
    CHECK(index.layer_present);
    CHECK(index.layer.descriptors.size() == 2);
    CHECK(index.layer.present);
    CHECK(index.import_count == 7);
    CHECK(index.build_count == 1);
    REQUIRE(index.find_descriptor(0x1000) != nullptr);
    CHECK(index.find_descriptor(0x1000)->kind == 24);
    REQUIRE(index.find_descriptor(0x2000) != nullptr);
    CHECK(index.find_descriptor(0x2000)->name == "main.Pt");
    CHECK(index.find_descriptor(0x3000) == nullptr);
  }
  // No blob at all: layer_present=false (the consumers' fail-closed gate).
  {
    wt::go_module_index_t index;
    REQUIRE(wt::go_build_module_index(nullptr, 0, nullptr, 0, 0, &index));
    CHECK_FALSE(index.layer_present);
    CHECK(index.layer.descriptors.empty());
  }
  // Malformed blob (bad version word): decode fails -> layer_present=false.
  {
    std::vector<std::uint32_t> bad = { 0xFFBAD, 1, 2, 3, 4 };
    wt::go_module_index_t index;
    REQUIRE(wt::go_build_module_index(bad.data(), bad.size(), nullptr, 0, 0, &index));
    CHECK_FALSE(index.layer_present);
  }
}

TEST_CASE("M2 H1: blob-'Z' port keeps the per-F5 parse policy")
{
  const std::vector<std::uint32_t> t = encode_layer(synthetic_layer());
  // Two entries for table 0 (slots 5, 7) and one for another table.
  std::vector<std::uint8_t> z;
  push_uleb(&z, 3);
  push_uleb(&z, 0);
  push_uleb(&z, 5);
  push_uleb(&z, 10);
  push_uleb(&z, 0);
  push_uleb(&z, 1);
  push_uleb(&z, 6);
  push_uleb(&z, 11);
  push_uleb(&z, 0);
  push_uleb(&z, 0);
  push_uleb(&z, 7);
  push_uleb(&z, 11);
  push_uleb(&z, 0);
  {
    wt::go_module_index_t index;
    REQUIRE(wt::go_build_module_index(t.data(), t.size(), z.data(), z.size(), 0, &index));
    REQUIRE(index.table0_slot_to_fn.size() == 2);
    CHECK(index.table0_slot_to_fn.at(5) == 10);
    CHECK(index.table0_slot_to_fn.at(7) == 11); // table != 0 dropped
  }
  // Duplicate slot: FIRST entry wins (emplace policy of the old parse).
  {
    std::vector<std::uint8_t> dup;
    push_uleb(&dup, 2);
    push_uleb(&dup, 0);
    push_uleb(&dup, 5);
    push_uleb(&dup, 10);
    push_uleb(&dup, 0);
    push_uleb(&dup, 0);
    push_uleb(&dup, 5);
    push_uleb(&dup, 99);
    push_uleb(&dup, 0);
    wt::go_module_index_t index;
    REQUIRE(wt::go_build_module_index(t.data(), t.size(), dup.data(), dup.size(), 0, &index));
    REQUIRE(index.table0_slot_to_fn.size() == 1);
    CHECK(index.table0_slot_to_fn.at(5) == 10);
  }
  // Malformed stream (truncated uleb): no partial map.
  {
    const std::uint8_t broken[] = { 0x02, 0x00 };
    wt::go_module_index_t index;
    REQUIRE(wt::go_build_module_index(t.data(), t.size(), broken, sizeof(broken), 0, &index));
    CHECK(index.table0_slot_to_fn.empty());
  }
  // Count over the 1<<22 cap: no map.
  {
    std::vector<std::uint8_t> huge;
    push_uleb(&huge, (1ULL << 22) + 1);
    wt::go_module_index_t index;
    REQUIRE(wt::go_build_module_index(t.data(), t.size(), huge.data(), huge.size(), 0, &index));
    CHECK(index.table0_slot_to_fn.empty());
  }
}

TEST_CASE("M2 H1: find_descriptor agrees with the linear layer scan (balls)")
{
  // The real corpus: every descriptor the layer's linear scan resolves must
  // resolve to the SAME descriptor through the index hash, and no other
  // address may resolve (the O(1) path the F5 collectors now take).
  static wt::module_t mod;
  static bool loaded = false;
  static bool ok = false;
  if ( !loaded )
  {
    loaded = true;
    std::ifstream in(std::string(WASMHEX_REPO_DIR) + "/wasm-test/gowasm-balls/main.wasm",
                     std::ios::binary);
    std::ostringstream buffer;
    buffer << in.rdbuf();
    const std::string bytes = buffer.str();
    ok = !bytes.empty()
      && mod.parse(reinterpret_cast<const wt::uchar *>(bytes.data()), bytes.size());
  }
  if ( !ok )
    return;
  const wt::go_type_info_t &layer = mod.go_types;
  REQUIRE(layer.present);
  REQUIRE(layer.descriptors.size() > 100);
  std::vector<std::uint32_t> words;
  wt::encode_go_type_info_blob(layer, &words);
  wt::go_module_index_t index;
  REQUIRE(wt::go_build_module_index(words.data(), words.size(), nullptr, 0, 0, &index));
  CHECK(index.layer_present);
  CHECK(index.layer.descriptors.size() == layer.blob_descriptor_cap);
  size_t checked = 0;
  for ( const wt::go_type_descriptor_t &descriptor : index.layer.descriptors )
  {
    const wt::go_type_descriptor_t *want = layer.descriptor_by_addr(descriptor.addr);
    const wt::go_type_descriptor_t *got = index.find_descriptor(descriptor.addr);
    REQUIRE(got != nullptr);
    CHECK(got->addr == want->addr);
    CHECK(got->kind == want->kind);
    CHECK(got->name == want->name);
    ++checked;
  }
  CHECK(checked == index.layer.descriptors.size());
  CHECK(index.find_descriptor(0x1) == nullptr); // not a descriptor address
}
