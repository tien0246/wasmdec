// ModuleIndex implementation — see index.hpp for the contract.
#include "index.hpp"

namespace wasmhex::loader
{

std::uint64_t go_fnv1a64(const std::uint8_t *bytes, std::size_t count, std::uint64_t seed)
{
  std::uint64_t hash = seed;
  if ( bytes != nullptr )
    for ( std::size_t i = 0; i < count; ++i )
    {
      hash ^= bytes[i];
      hash *= 0x100000001b3ULL;
    }
  return hash;
}

// Identity/fingerprint hasher: a word-stride murmur-style mix (~1 cycle per
// byte — the module blobs reach megabytes and the identity runs on every
// F5). Not cryptographic; it exists to catch ANY content change cheaply,
// with the length mixed in so [ab][c] can never alias [a][bc].
std::uint64_t go_hash64(const std::uint8_t *bytes, std::size_t count, std::uint64_t seed)
{
  std::uint64_t hash = seed ^ (std::uint64_t(count) * 0x9e3779b97f4a7c15ULL);
  if ( bytes != nullptr )
  {
    std::size_t i = 0;
    for ( ; i + 8 <= count; i += 8 )
    {
      std::uint64_t word = 0;
      __builtin_memcpy(&word, bytes + i, sizeof(word));
      hash ^= word;
      hash *= 0xff51afd7ed558ccdULL;
      hash ^= hash >> 29;
    }
    if ( i < count )
    {
      std::uint64_t tail = 0;
      __builtin_memcpy(&tail, bytes + i, count - i);
      hash ^= tail;
      hash *= 0xff51afd7ed558ccdULL;
      hash ^= hash >> 29;
    }
  }
  return hash;
}

namespace
{
// Hash one length-prefixed ingredient (length first so [ab][c] can never
// alias [a][bc]).
void mix_bytes(std::uint64_t *hash, const std::uint8_t *bytes, std::size_t count)
{
  *hash = go_hash64(bytes, count, *hash);
}
} // namespace

std::uint64_t go_module_identity(const std::uint32_t *t_words, std::size_t t_word_count,
                                 const std::uint8_t *z_bytes, std::size_t z_byte_count,
                                 std::uint32_t import_count, std::uint32_t index_schema)
{
  std::uint64_t hash = 0xcbf29ce484222325ULL;
  mix_bytes(&hash, reinterpret_cast<const std::uint8_t *>(t_words),
            t_word_count * sizeof(std::uint32_t));
  mix_bytes(&hash, z_bytes, z_byte_count);
  const std::uint8_t tail[8]
    = { std::uint8_t(import_count),       std::uint8_t(import_count >> 8),
        std::uint8_t(import_count >> 16), std::uint8_t(import_count >> 24),
        std::uint8_t(index_schema),       std::uint8_t(index_schema >> 8),
        std::uint8_t(index_schema >> 16), std::uint8_t(index_schema >> 24) };
  mix_bytes(&hash, tail, sizeof(tail));
  return hash;
}

const go_type_descriptor_t *go_module_index_t::find_descriptor(std::uint64_t addr) const
{
  const auto found = descriptor_index.find(addr);
  if ( found == descriptor_index.end() || found->second >= layer.descriptors.size() )
    return nullptr;
  return &layer.descriptors[found->second];
}

namespace
{
// Blob 'Z' (table slots) — ported byte-for-byte from the per-F5 parse the
// decompiler used to run (filter.cpp load_table0_slots): uleb
// [count][{table, slot, fn, type_idx}] * count, table 0 only, first entry
// for a slot wins, count capped at 1<<22. Returns false on any malformed
// stream or an empty result — the callers then treat the map as absent.
bool parse_table0_slots(const std::uint8_t *cursor, const std::uint8_t *end,
                        std::unordered_map<std::uint32_t, std::uint32_t> *out)
{
  const auto read_uleb = [&](std::uint64_t *value) -> bool
  {
    std::uint64_t result = 0;
    unsigned shift = 0;
    for ( int i = 0; i < 10 && cursor < end; ++i )
    {
      const std::uint8_t byte = *cursor++;
      result |= std::uint64_t(byte & 0x7f) << shift;
      if ( (byte & 0x80) == 0 )
      {
        *value = result;
        return true;
      }
      shift += 7;
    }
    return false;
  };
  std::uint64_t count = 0;
  if ( !read_uleb(&count) || count > (1u << 22) )
    return false;
  for ( std::uint64_t i = 0; i < count; ++i )
  {
    std::uint64_t table = 0, slot = 0, fn = 0, type_idx = 0;
    if ( !read_uleb(&table) || !read_uleb(&slot) || !read_uleb(&fn) || !read_uleb(&type_idx) )
      return false;
    if ( table == 0 && slot <= UINT32_MAX && fn <= UINT32_MAX )
      out->emplace(std::uint32_t(slot), std::uint32_t(fn));
  }
  return !out->empty();
}
} // namespace

bool go_build_module_index(const std::uint32_t *t_words, std::size_t t_word_count,
                           const std::uint8_t *z_bytes, std::size_t z_byte_count,
                           std::uint32_t import_count, go_module_index_t *out)
{
  if ( out == nullptr )
    return false;
  *out = go_module_index_t{};
  out->import_count = import_count;
  out->build_count = 1;
  if ( t_words != nullptr && t_word_count != 0
       && decode_go_type_info_blob(t_words, t_word_count, &out->layer) )
    out->layer_present = true;
  out->descriptor_index.reserve(out->layer.descriptors.size());
  for ( std::size_t i = 0; i < out->layer.descriptors.size(); ++i )
  {
    // First descriptor for an address wins — the linear scan the layer's
    // descriptor_by_addr applies (front to back) resolves the same way.
    out->descriptor_index.emplace(out->layer.descriptors[i].addr, i);
  }
  if ( z_bytes != nullptr && z_byte_count != 0 )
  {
    // A malformed 'Z' stream leaves the map empty (the old parse returned
    // false and every caller skipped) — never a partial map.
    std::unordered_map<std::uint32_t, std::uint32_t> slots;
    if ( !parse_table0_slots(z_bytes, z_bytes + z_byte_count, &slots) )
      slots.clear();
    out->table0_slot_to_fn = std::move(slots);
  }
  return true;
}

std::size_t function_ir_t::find(std::uint64_t ea) const
{
  std::size_t lo = 0;
  std::size_t hi = insns.size();
  while ( lo < hi )
  {
    const std::size_t mid = (lo + hi) / 2;
    if ( insns[mid].ea < ea )
      lo = mid + 1;
    else
      hi = mid;
  }
  if ( lo < insns.size() && insns[lo].ea == ea )
    return lo;
  return SIZE_MAX;
}

} // namespace wasmhex::loader
