#include "../../../ida/processor/internal.hpp"
#include "metadata.hpp"

#include <algorithm>
#include <cstring>
#include <list>
#include <unordered_map>
#include <vector>

namespace wasmhex
{
namespace
{
// Leading version byte of every versioned metadata blob (see
// ida/metadata.hpp for the per-tag wire layouts).
constexpr uchar kMetadataV1 = 0x01;

bool tag_is_versioned(uchar tag)
{
  return tag == 'C' || tag == 'c' || tag == 'D' || tag == 'T' || tag == 'S';
}

bool tag_migrates_on_read(uchar tag)
{
  return tag == 'C' || tag == 'c' || tag == 'D' || tag == 'T' || tag == 'S';
}

bool read_sup_ea_blob(netnode &node, ea_t key, uchar tag, bytevec_t *out)
{
  const ssize_t size = node.supval_ea(key, nullptr, 0, tag);
  if ( size <= 0 )
    return false;
  out->resize_noinit(size_t(size));
  return node.supval_ea(key, out->begin(), out->size(), tag) == size;
}

// Best-effort v0 -> v1 rewrite (same netnode key/tag, supval space); fails
// silently on read-only databases — the caller already has the v0 data.
void migrate_sup_ea_prefix(netnode &node, ea_t key, uchar tag, const bytevec_t &v0)
{
  bytevec_t v1;
  v1.resize_noinit(v0.size() + 1);
  v1[0] = kMetadataV1;
  memcpy(&v1[1], v0.begin(), v0.size());
  node.supset_ea(key, v1.begin(), v1.size(), tag);
}

uint32_t decode_u32le(const uchar *data)
{
  uint32_t value = 0;
  for ( unsigned byte = 0; byte < 4; ++byte )
    value |= uint32(data[byte]) << (byte * 8);
  return value;
}
} // namespace

bool valid_fixed_memory_extent(uchar memory_mode, uint64 memory_bytes)
{
  return memory_mode == 1
    ? memory_bytes <= (uint64(1) << 32) && (memory_bytes & 0xFFFF) == 0
    : memory_mode == 2 && (memory_bytes == UINT64_MAX || (memory_bytes & 0xFFFF) == 0);
}

bool get_wasm_memory_extent(netnode &wasm_node, uchar memory_mode, uint64 *memory_bytes,
                            bool *growable)
{
  if ( wasm_node == BADNODE || memory_bytes == nullptr )
    return false;
  uint64 fixed_bytes = 0;
  if ( wasm_node.supval(0, &fixed_bytes, sizeof(fixed_bytes), 'm') == sizeof(fixed_bytes) )
  {
    if ( !valid_fixed_memory_extent(memory_mode, fixed_bytes) )
      return false;
    *memory_bytes = fixed_bytes;
    if ( growable != nullptr )
      *growable = false;
    return true;
  }
  uint64 maximum_pages = 0;
  if ( (memory_mode != 1 && memory_mode != 2)
       || wasm_node.supval(0, &maximum_pages, sizeof(maximum_pages), 'n') != sizeof(maximum_pages)
       || maximum_pages == 0 || maximum_pages > wasmhex_max_growable_memory_pages )
    return false;
  *memory_bytes = maximum_pages * 65536;
  if ( growable != nullptr )
    *growable = true;
  return valid_fixed_memory_extent(memory_mode, *memory_bytes);
}

bool fixed_memory_extent_fits_ida(uchar memory_mode, uint64 memory_bytes)
{
  return valid_fixed_memory_extent(memory_mode, memory_bytes)
    && memory_bytes <= UINT64_MAX - wasmhex_memory_base;
}

bool get_wasm_bounds_mode(netnode &wasm_node, wasmhex_bounds_mode_t *mode)
{
  if ( wasm_node == BADNODE || mode == nullptr )
    return false;
  uchar raw = 0;
  const ssize_t size = wasm_node.supval(0, &raw, sizeof(raw), 'w');
  if ( size < 0 )
  {
    *mode = wasmhex_bounds_mode_t::explicit_bounds;
    return true;
  }
  if ( size != ssize_t(sizeof(raw)) || raw > 2 )
    return false;
  *mode = static_cast<wasmhex_bounds_mode_t>(int(raw));
  return true;
}

// Microcode helpers (mop_t/mcallinfo_t/reg2mreg) link against the hexrays
// plugin; the loader target compiles metadata.cpp without hexrays
// (WASMHEX_NO_HEXRAYS) and never calls them.
#ifndef WASMHEX_NO_HEXRAYS
bool make_wasm_memory_selector(mop_t *selector, ea_t ea, wasmhex_bounds_mode_t bounds_mode,
                               bool constant_address)
{
  netnode wasm_node(wasmhex_node_name, 0, false);
  sel_t memory_selector = BADSEL;
  if ( selector == nullptr || wasm_node == BADNODE
       || wasm_node.supval(0, &memory_selector, sizeof(memory_selector), 'Y')
         != sizeof(memory_selector)
       || memory_selector == BADSEL || memory_selector > UINT16_MAX )
    return false;
  if ( bounds_mode == wasmhex_bounds_mode_t::hidden && !constant_address )
  {
    selector->make_reg(reg2mreg(wasmhex_ds_reg), 2);
    return true;
  }
  selector->make_number(memory_selector, 2, ea, 0);
  return true;
}
#endif // WASMHEX_NO_HEXRAYS

wasm_table_metadata_status_t get_wasm_table_metadata(netnode &wasm_node, uint64 table_index,
                                                     uint32 *table_count, uint64 *minimum,
                                                     uchar *flags)
{
  uint32 count = 0;
  if ( wasm_node == BADNODE || wasm_node.supval(0, &count, sizeof(count), 'x') != sizeof(count) )
    return wasm_table_metadata_status_t::missing;
  if ( table_count != nullptr )
    *table_count = count;
  if ( table_index >= count )
    return wasm_table_metadata_status_t::out_of_range;
  uint64 table_minimum = 0;
  uchar table_flags = 0;
  if ( wasm_node.supval(uint32(table_index), &table_minimum, sizeof(table_minimum), 'y')
         != sizeof(table_minimum)
       || wasm_node.supval(uint32(table_index), &table_flags, sizeof(table_flags), 'z')
         != sizeof(table_flags)
       || (table_flags & ~uchar(7)) != 0 || ((table_flags & 4) != 0 && (table_flags & 1) == 0) )
    return wasm_table_metadata_status_t::malformed;
  if ( minimum != nullptr )
    *minimum = table_minimum;
  if ( flags != nullptr )
    *flags = table_flags;
  return wasm_table_metadata_status_t::ok;
}

bool read_uleb(insn_t *insn, uint64 *value, unsigned bits)
{
  if ( bits == 0 || bits > 64 )
    return false;
  uint64 result = 0;
  unsigned shift = 0;
  const unsigned max_bytes = (bits + 6) / 7;
  for ( unsigned i = 0; i < max_bytes; ++i )
  {
    if ( !is_loaded(insn->ea + insn->size) )
      return false;
    const uchar byte = insn->get_next_byte();
    const uint64 part = byte & 0x7F;
    const unsigned remaining = bits - shift;
    if ( remaining < 7 && (part >> remaining) != 0 )
      return false;
    result |= part << shift;
    if ( (byte & 0x80) == 0 )
    {
      *value = result;
      return true;
    }
    shift += 7;
  }
  return false;
}

bool read_sleb(insn_t *insn, int64 *value, unsigned max_bytes)
{
  uint64 result = 0;
  unsigned shift = 0;
  for ( unsigned i = 0; i < max_bytes; ++i )
  {
    if ( !is_loaded(insn->ea + insn->size) )
      return false;
    const uchar byte = insn->get_next_byte();
    result |= uint64(byte & 0x7F) << shift;
    shift += 7;
    if ( (byte & 0x80) == 0 )
    {
      if ( (byte & 0x40) != 0 && shift < 64 )
        result |= (~uint64(0)) << shift;
      *value = int64(result);
      return true;
    }
  }
  return false;
}

bool decode_insn_value_type(insn_t *insn, wasmhex::value_type_t *out)
{
  if ( insn == nullptr || out == nullptr )
    return false;
  // Longest encoding: 0x63/0x64 prefix plus a 5-byte s33 heap type.
  uchar buffer[6];
  size_t available = 0;
  for ( ; available < sizeof(buffer); ++available )
  {
    if ( !is_loaded(insn->ea + insn->size + available) )
      break;
    buffer[available] = get_byte(insn->ea + insn->size + available);
  }
  size_t pos = 0;
  if ( wasmhex::decode_value_type(buffer, available, &pos, out) != wasmhex::vt_ok )
    return false;
  insn->size += unsigned(pos);
  return true;
}

bool get_ea_list(ea_t key, uchar tag, qvector<ea_t> *values)
{
  netnode wasm_node(wasmhex_node_name, 0, false);
  if ( wasm_node == BADNODE )
    return false;
  bytevec_t raw;
  if ( !read_sup_ea_blob(wasm_node, key, tag, &raw) )
    return false;
  const uchar *payload = raw.begin();
  size_t payload_size = raw.size();
  bool versioned = false;
  // v1 framing is size-distinguishable: a v0 ea array is N*sizeof(ea_t)
  // bytes, v1 is 1 + N*sizeof(ea_t).
  if ( tag_is_versioned(tag) && payload_size > 1 && payload[0] == kMetadataV1
       && (payload_size - 1) % sizeof(ea_t) == 0 )
  {
    ++payload;
    --payload_size;
    versioned = true;
  }
  else if ( payload_size == 0 || payload_size % sizeof(ea_t) != 0 )
    return false;
  values->resize(payload_size / sizeof(ea_t));
  memcpy(values->begin(), payload, payload_size);
  if ( !versioned && tag_migrates_on_read(tag) )
    migrate_sup_ea_prefix(wasm_node, key, tag, raw);
  return true;
}

bool get_branch_table_metadata(ea_t key, qvector<ea_t> *targets, qvector<uint32> *depths)
{
  netnode wasm_node(wasmhex_node_name, 0, false);
  if ( wasm_node == BADNODE || targets == nullptr || depths == nullptr )
    return false;
  bytevec_t encoded;
  if ( wasm_node.getblob_ea(&encoded, key, 'c') <= 0 )
    return false;
  const uchar *const data = encoded.begin();
  const size_t size = encoded.size();
  const size_t entry = 4 + sizeof(ea_t);
  const uchar *payload = data;
  bool versioned = false;
  // v1 first ([0x01|count|depths|targets]), then the v0 layout whose first
  // byte is the count's low byte; both are validated against the exact
  // blob size, so the two parses can never both match (sizes differ by 1).
  if ( size >= 5 && data[0] == kMetadataV1 && (size - 5) % entry == 0
       && decode_u32le(data + 1) == uint32((size - 5) / entry) )
  {
    payload = data + 1;
    versioned = true;
  }
  else if ( size < 4 || (size - 4) % entry != 0
            || decode_u32le(data) != uint32((size - 4) / entry) )
    return false;
  const uint32 count = decode_u32le(payload);
  targets->resize(count);
  depths->resize(count);
  memcpy(depths->begin(), payload + 4, size_t(count) * sizeof(uint32));
  memcpy(targets->begin(), payload + 4 + size_t(count) * sizeof(uint32),
         size_t(count) * sizeof(ea_t));
  if ( !versioned )
  {
    bytevec_t v1;
    v1.resize_noinit(size + 1);
    v1[0] = kMetadataV1;
    memcpy(&v1[1], data, size);
    wasm_node.setblob_ea(v1.begin(), v1.size(), key, 'c');
  }
  return !targets->empty();
}

bool get_u32_list(ea_t key, uchar tag, qvector<uint32> *values)
{
  netnode wasm_node(wasmhex_node_name, 0, false);
  if ( wasm_node == BADNODE )
    return false;
  bytevec_t raw;
  if ( !read_sup_ea_blob(wasm_node, key, tag, &raw) )
    return false;
  const uchar *payload = raw.begin();
  size_t payload_size = raw.size();
  bool versioned = false;
  if ( tag_is_versioned(tag) && payload_size > 1 && payload[0] == kMetadataV1
       && (payload_size - 1) % sizeof(uint32) == 0 )
  {
    ++payload;
    --payload_size;
    versioned = true;
  }
  else if ( payload_size == 0 || payload_size % sizeof(uint32) != 0 )
    return false;
  values->resize(payload_size / sizeof(uint32));
  memcpy(values->begin(), payload, payload_size);
  if ( !versioned && tag_migrates_on_read(tag) )
    migrate_sup_ea_prefix(wasm_node, key, tag, raw);
  return true;
}

bool decode_type_vector(const uchar *data, size_t size, qvector<uchar> *types)
{
  size_t pos = 0;
  uint64 count = 0;
  unsigned shift = 0;
  for ( unsigned i = 0; i < 5; ++i )
  {
    if ( pos == size )
      return false;
    const uchar byte = data[pos++];
    count |= uint64(byte & 0x7F) << shift;
    if ( (byte & 0x80) == 0 )
    {
      if ( count != size - pos )
        return false;
      types->resize(size_t(count));
      if ( count != 0 )
        memcpy(types->begin(), data + pos, size_t(count));
      return true;
    }
    shift += 7;
  }
  return false;
}

bool get_type_vector(netnode &wasm_node, uint32 index, uchar tag, qvector<uchar> *types)
{
  const ssize_t size = wasm_node.supval(index, nullptr, 0, tag);
  if ( size <= 0 )
    return false;
  bytevec_t encoded;
  encoded.resize_noinit(size_t(size));
  return wasm_node.supval(index, encoded.begin(), encoded.size(), tag) == size
    && decode_type_vector(encoded.begin(), encoded.size(), types);
}

bool get_type_vector(netnode &wasm_node, ea_t ea, uchar tag, qvector<uchar> *types)
{
  if ( tag == 'T' )
  {
    // Versioned slot, v1 first: [0x01][uleb count|types]. The only
    // theoretical overlap with v0 is the 2-byte blob [0x01 0x00], which as
    // v0 would mean "one type 0x00" — not a wasm value type, so it cannot
    // occur; the empty v1 vector [0x01 0x00] is common, hence this order.
    // Reading v0 migrates the blob to v1 in place.
    bytevec_t encoded;
    if ( !read_sup_ea_blob(wasm_node, ea, tag, &encoded) )
      return false;
    if ( encoded.size() > 1 && encoded[0] == kMetadataV1
         && decode_type_vector(encoded.begin() + 1, encoded.size() - 1, types) )
      return true;
    if ( !decode_type_vector(encoded.begin(), encoded.size(), types) )
      return false;
    bytevec_t v1;
    v1.resize_noinit(encoded.size() + 1);
    v1[0] = kMetadataV1;
    memcpy(&v1[1], encoded.begin(), encoded.size());
    wasm_node.supset_ea(ea, v1.begin(), v1.size(), tag);
    return true;
  }
  const ssize_t size = wasm_node.supval_ea(ea, nullptr, 0, tag);
  if ( size <= 0 )
    return false;
  bytevec_t encoded;
  encoded.resize_noinit(size_t(size));
  return wasm_node.supval_ea(ea, encoded.begin(), encoded.size(), tag) == size
    && decode_type_vector(encoded.begin(), encoded.size(), types);
}

bool decode_reference_metadata(const uchar *data, size_t size, size_t expected_count,
                               qvector<int64> *heap_types, qvector<uchar> *ref_forms)
{
  if ( heap_types == nullptr || ref_forms == nullptr )
    return false;
  size_t pos = 0;
  uint64 count = 0;
  unsigned shift = 0;
  bool count_decoded = false;
  for ( unsigned i = 0; i < 5; ++i )
  {
    if ( pos == size )
      return false;
    const uchar byte = data[pos++];
    count |= uint64(byte & 0x7F) << shift;
    if ( (byte & 0x80) == 0 )
    {
      count_decoded = true;
      break;
    }
    shift += 7;
  }
  if ( !count_decoded || count != expected_count )
    return false;
  heap_types->resize(size_t(count), -1);
  ref_forms->resize(size_t(count), 0);
  for ( size_t i = 0; i < size_t(count); ++i )
  {
    if ( pos == size )
      return false;
    const uchar form = data[pos++];
    if ( form > 2 )
      return false;
    (*ref_forms)[i] = form;
    if ( form != 0 )
    {
      if ( size - pos < 8 )
        return false;
      uint64 raw = 0;
      for ( unsigned byte = 0; byte < 8; ++byte )
        raw |= uint64(data[pos++]) << (byte * 8);
      if ( int64(raw) == -1 )
        return false;
      (*heap_types)[i] = int64(raw);
    }
  }
  return pos == size;
}

bool get_reference_metadata(netnode &wasm_node, ea_t ea, uchar tag, size_t expected_count,
                            qvector<int64> *heap_types, qvector<uchar> *ref_forms)
{
  if ( wasm_node == BADNODE || heap_types == nullptr || ref_forms == nullptr )
    return false;
  bytevec_t encoded;
  if ( wasm_node.getblob_ea(&encoded, ea, tag) > 0 )
    return decode_reference_metadata(encoded.begin(), encoded.size(), expected_count, heap_types,
                                     ref_forms);
  const ssize_t size = wasm_node.supval_ea(ea, nullptr, 0, tag);
  if ( size > 0 )
  {
    encoded.resize_noinit(size_t(size));
    return wasm_node.supval_ea(ea, encoded.begin(), encoded.size(), tag) == size
      && decode_reference_metadata(encoded.begin(), encoded.size(), expected_count, heap_types,
                                   ref_forms);
  }
  heap_types->resize(expected_count, -1);
  ref_forms->resize(expected_count, 0);
  return true;
}

// wasm_type_size / tuple_type_name / multi_result_word_count /
// tuple_layout_matches / get_multi_result_type / make_multi_return_location
// moved to ida/abi.cpp: one definition shared by the loader and the
// processor module.

size_t wasm_results_size(const qvector<uchar> &types)
{
  size_t size = 0;
  for ( uchar type : types )
    size += wasm_type_size(type);
  return size;
}

#ifndef WASMHEX_NO_HEXRAYS
bool set_multi_return_regs(mcallinfo_t *callinfo, const qvector<uchar> &result_types)
{
  size_t words = 0;
  if ( !multi_result_word_count(result_types, &words) )
    return false;
  callinfo->return_regs.clear();
  callinfo->retregs.clear();
  auto append = [&](const argloc_t &location, int size)
  {
    mop_t retreg;
    if ( size == 4 && location.is_reg1() )
    {
      const mreg_t reg = reg2mreg(location.reg1());
      callinfo->return_regs.add(reg, size);
      retreg.make_reg(reg, size);
    }
    else if ( size == 8 && location.is_reg2() )
    {
      const mreg_t low = reg2mreg(location.reg1());
      const mreg_t high = reg2mreg(location.reg2());
      callinfo->return_regs.add(low, 4);
      callinfo->return_regs.add(high, 4);
      retreg.make_reg_pair(low, high, 4);
    }
    else
      return false;
    callinfo->retregs.push_back(retreg);
    return true;
  };

  const argloc_t &return_location = callinfo->return_argloc;
  size_t total_size = 0;
  if ( return_location.is_scattered() )
  {
    const scattered_aloc_t &parts = return_location.scattered();
    qvector<argpart_t> sorted;
    sorted.reserve(parts.size());
    for ( size_t i = 0; i < parts.size(); ++i )
      sorted.push_back(parts[i]);
    std::sort(sorted.begin(), sorted.end(),
              [](const argpart_t &a, const argpart_t &b) { return a.off < b.off; });
    for ( const argpart_t &part : sorted )
    {
      if ( !append(part, part.size) )
        return false;
      total_size += part.size;
    }
  }
  else
  {
    const int size = int(callinfo->return_type.get_size());
    if ( !append(return_location, size) )
      return false;
    total_size = size;
  }
  size_t expected_size = 0;
  for ( uchar type : result_types )
    expected_size += wasm_type_size(type);
  if ( total_size != expected_size || words * 4 != expected_size )
    return false;
  callinfo->spoiled.add(callinfo->return_regs);
  callinfo->flags |= FCI_EXPLOCS;
  return true;
}
#endif // WASMHEX_NO_HEXRAYS

#ifndef WASMHEX_NO_HEXRAYS
bool get_multi_result_component(const mcallinfo_t &callinfo, size_t offset, int size, mop_t *out)
{
  auto get_word = [&](size_t byte_offset, mreg_t *reg)
  {
    auto from_part = [&](const argloc_t &location, size_t part_offset, size_t part_size)
    {
      if ( byte_offset < part_offset || byte_offset + 4 > part_offset + part_size )
        return false;
      const size_t within = byte_offset - part_offset;
      if ( within == 0 && location.is_reg1() )
      {
        *reg = reg2mreg(location.reg1());
        return true;
      }
      if ( location.is_reg2() && (within == 0 || within == 4) )
      {
        *reg = reg2mreg(within == 0 ? location.reg1() : location.reg2());
        return true;
      }
      return false;
    };

    if ( callinfo.return_argloc.is_scattered() )
    {
      const scattered_aloc_t &parts = callinfo.return_argloc.scattered();
      for ( size_t i = 0; i < parts.size(); ++i )
        if ( from_part(parts[i], parts[i].off, parts[i].size) )
          return true;
      return false;
    }
    return from_part(callinfo.return_argloc, 0, callinfo.return_type.get_size());
  };

  mreg_t low = mr_none;
  if ( (size != 4 && size != 8) || !get_word(offset, &low) )
    return false;
  if ( size == 4 )
  {
    out->make_reg(low, size);
    return true;
  }
  mreg_t high = mr_none;
  if ( !get_word(offset + 4, &high) )
    return false;
  out->make_reg_pair(low, high, 4);
  return true;
}
#endif // WASMHEX_NO_HEXRAYS

bool get_ea_value(ea_t key, uchar tag, ea_t *value)
{
  netnode wasm_node(wasmhex_node_name, 0, false);
  if ( wasm_node == BADNODE )
    return false;
  bytevec_t raw;
  if ( !read_sup_ea_blob(wasm_node, key, tag, &raw) )
    return false;
  const uchar *payload = raw.begin();
  size_t payload_size = raw.size();
  // Double-read for the ea-value versioned slots; sizes are exact so the
  // formats cannot be confused. No v0 migration here: the single-ea 'C'/'S'
  // slots still have v0 writers and are
  // also read raw outside this file.
  if ( (tag == 'C' || tag == 'S') && payload_size == 1 + sizeof(ea_t) && payload[0] == kMetadataV1 )
  {
    ++payload;
    --payload_size;
  }
  if ( payload_size != sizeof(ea_t) )
    return false;
  memcpy(value, payload, sizeof(*value));
  return true;
}

bool get_dispatch_case_count(netnode &wasm_node, ea_t key, uint32 *case_count)
{
  if ( case_count == nullptr )
    return false;
  const ssize_t size = wasm_node.supval_ea(key, case_count, sizeof(*case_count), 'Q');
  if ( size != sizeof(*case_count) )
  {
    msg(
      "WASMHEX_F5_UNSUPPORTED: sparse dispatch case-count metadata is missing or malformed at %a\n",
      key);
    return false;
  }
  return true;
}

bool get_global_metadata(uint32 index, uchar *type, uchar *mutability, ea_t *address,
                         bool *imported, qstring *import_helper)
{
  netnode wasm_node(wasmhex_node_name, 0, false);
  if ( wasm_node == BADNODE || type == nullptr || mutability == nullptr || address == nullptr
       || imported == nullptr || import_helper == nullptr
       || wasm_node.supval(index, type, sizeof(*type), 'g') != sizeof(*type)
       || wasm_node.supval(index, mutability, sizeof(*mutability), 'h') != sizeof(*mutability) )
    return false;
  if ( *type != 0x7F && *type != 0x7E && *type != 0x7D && *type != 0x7C )
    return false;
  if ( *mutability > 1 )
    return false;
  *address = BADADDR;
  *imported = false;
  import_helper->clear();

  const ssize_t address_size = wasm_node.supval(index, nullptr, 0, 'j');
  if ( address_size > 0 )
  {
    return address_size == ssize_t(sizeof(*address))
      && wasm_node.supval(index, address, sizeof(*address), 'j') == ssize_t(sizeof(*address))
      && *address != BADADDR && *address <= UINT32_MAX;
  }

  const ssize_t helper_size = wasm_node.supval(index, nullptr, 0, 'k');
  if ( helper_size > 0 )
  {
    bytevec_t encoded;
    encoded.resize_noinit(size_t(helper_size));
    if ( wasm_node.supval(index, encoded.begin(), encoded.size(), 'k') != helper_size
         || encoded.back() != 0 )
      return false;
    *import_helper = qstring(reinterpret_cast<const char *>(encoded.begin()), encoded.size() - 1);
    *imported = true;
    *address = BADADDR;
    return !import_helper->empty();
  }
  return false;
}

bool is_declared_function(uint32 index)
{
  netnode wasm_node(wasmhex_node_name, 0, false);
  uchar marker = 0;
  return wasm_node != BADNODE
    && wasm_node.supval(index, &marker, sizeof(marker), 'e') == sizeof(marker) && marker == 1;
}

bool is_function_end(ea_t ea)
{
  netnode wasm_node(wasmhex_node_name, 0, false);
  uchar marker = 0;
  if ( wasm_node != BADNODE
       && wasm_node.supval_ea(ea, &marker, sizeof(marker), 'W') == sizeof(marker) && marker == 1 )
    return true;
  func_entry_info_t function;
  return get_func_entry_info(&function, ea) && function.end_ea == ea + get_item_size(ea);
}

bool get_wasm_tinfo(uchar type, tinfo_t *tif)
{
  const char *decl = type == 0x7F                                  ? "int"
    : type == 0x7E                                                 ? "__int64"
    : type == 0x7D                                                 ? "float"
    : type == 0x7C                                                 ? "double"
    : type == 0x70 || type == 0x6F || type == 0x71 || type == 0x72 ? "unsigned int"
                                                                   : nullptr;
  return decl != nullptr && parse_decl(tif, nullptr, get_idati(), decl, PT_SIL | PT_SEMICOLON);
}

uchar get_wasm_type(const tinfo_t &tif)
{
  if ( tif.is_floating() )
    return tif.get_size() == 4 ? 0x7D : tif.get_size() == 8 ? 0x7C : 0;
  if ( tif.is_integral() )
    return tif.get_size() == 4 ? 0x7F : tif.get_size() == 8 ? 0x7E : 0;
  return 0;
}

bool make_parsed_wasm_function_type(const qvector<uchar> &params, const qvector<uchar> &results,
                                    bool indirect, tinfo_t *out, bool force_sret)
{
  return make_parsed_wasm_function_type_ex(params, results, indirect, out, force_sret, nullptr);
}

// The implementation below with a NAMED sret pointee
// (`struct go__X *result` instead of `void *result`). Arglocs are
// identical — both are pointer-sized — so the decompiler-side derived
// signature stays consistent with the applied tinfo.
bool make_parsed_wasm_function_type_ex(const qvector<uchar> &params, const qvector<uchar> &results,
                                       bool indirect, tinfo_t *out, bool force_sret,
                                       const char *sret_pointee)
{
  const bool sret = force_sret || (results.size() > 1 && wasm_results_size(results) > 16);
  qstring declaration(sret || results.empty() ? "void"
                        : results[0] == 0x7E  ? "__int64"
                        : results[0] == 0x7D  ? "float"
                        : results[0] == 0x7C  ? "double"
                                              : "int");
  qstring type_name("wasm_type");
  auto append_type = [&](uchar type)
  {
    qstring part;
    part.sprnt("_%02x", unsigned(type));
    type_name.append(part);
  };
  if ( indirect )
    type_name.append("_x");
  for ( uchar type : params )
    append_type(type);
  type_name.append("_r");
  for ( uchar type : results )
    append_type(type);
  declaration.append(" __usercall ");
  declaration.append(type_name);
  if ( !sret && results.size() == 1 )
    declaration.append(wasm_type_size(results[0]) == 8 ? "@<r0:r1>" : "@<r0>");
  declaration.append("(");
  size_t word = 0;
  bool has_argument = false;
  auto append_argument = [&](const char *type, int size, const char *name)
  {
    if ( !append_wasm_user_argument(&declaration, type, name, word, size, has_argument) )
      return false;
    word += wasm_argument_word_count(word, size);
    has_argument = true;
    return true;
  };
  if ( indirect && !append_argument("unsigned int", 4, "element_index") )
    return false;
  for ( size_t i = 0; i < params.size(); ++i )
  {
    const uchar type = params[i];
    const char *type_name = type == 0x7F                             ? "int"
      : type == 0x7E                                                 ? "__int64"
      : type == 0x7D                                                 ? "float"
      : type == 0x7C                                                 ? "double"
      : type == 0x70 || type == 0x6F || type == 0x71 || type == 0x72 ? "unsigned int"
                                                                     : nullptr;
    qstring name;
    name.sprnt("a%zu", i + 1);
    if ( type_name == nullptr
         || !append_argument(type_name, type == 0x7E || type == 0x7C ? 8 : 4, name.c_str()) )
      return false;
  }
  if ( sret )
  {
    // A proven named Go result renders the sret pointer as
    // `struct go__X *result` (the til struct exists before this parse);
    // everything else stays `void *result`.
    qstring result_decl("void *");
    if ( sret_pointee != nullptr && sret_pointee[0] != '\0' )
      result_decl.sprnt("struct %s *", sret_pointee);
    if ( !append_argument(result_decl.c_str(), 4, "result") )
      return false;
  }
  if ( !has_argument )
    declaration.append("void");
  declaration.append(");");
  if ( !parse_decl(out, nullptr, get_idati(), declaration.c_str(), PT_SIL | PT_SEMICOLON) )
    return false;
  func_type_data_t function;
  if ( !out->get_func_details(&function, GTD_CALC_ARGLOCS) )
    return false;
  word = 0;
  const size_t count = params.size() + size_t(indirect) + size_t(sret);
  for ( size_t i = 0; i < count; ++i )
  {
    const int size = int(function[i].type.get_size());
    if ( size != 4 && size != 8 || !make_wasm_argument_location(word, size, &function[i].argloc) )
      return false;
    word += wasm_argument_word_count(word, size);
  }
  for ( const funcarg_t &argument : function )
    if ( argument.argloc.is_stkoff() )
      function.stkargs
        = std::max(function.stkargs, uval_t(argument.argloc.stkoff() + argument.type.get_size()));
  function.flags |= FTI_ARGLOCS | FTI_EXPLOCS;
  return out->create_func(function);
  return true;
}

bool make_wasm_go_sret_function_type(const qvector<uchar> &params, const qvector<uchar> &results,
                                     tinfo_t *out)
{
  if ( results.size() <= 1 )
    return false;
  return make_parsed_wasm_function_type(params, results, false, out, /*force_sret=*/true);
}

namespace
{
// Build marker verified via strings on the deployed dylib (pattern
// from ir.cpp / database.cpp).
extern "C" __attribute__((used)) const char kWasmhexG7NamedProtoBuildMarker[]
  = "WASMHEX_BUILD_MARKER_G7_NAMED_PROTOTYPES";
} // namespace

// Materialize (or replace) a virtual Go struct in the til: go__string
// {ptr,len}, go__js__Value {ref,gcPtr}, go__main__Ball {x,y,...} — field
// names/types come from the decoded descriptor (core types layer). Each
// name is created ONCE per database (memoized): repeated set_named_type
// churn during the load loop renumbers til ordinals and corrupts the
// tinfos already applied to other functions (the -31 MERR_ARCH lesson
// from this slice's own bar run).
bool ensure_go_struct_type(const char *name, const std::vector<loader::go_c_field_t> &fields,
                           uint64 total_size)
{
  if ( name == nullptr || fields.empty() || total_size == 0 || total_size > (1ull << 20) )
    return false;
  // Already present with the right size: never touch the til again.
  tinfo_t existing;
  if ( existing.get_named_type(get_idati(), name) && existing.get_size() == total_size )
    return true;
  udt_type_data_t udt;
  udt.effalign = 1;
  udt.taudt_bits |= TAUDT_UNALIGNED;
  udt.total_size = total_size;
  for ( const loader::go_c_field_t &field : fields )
  {
    if ( field.size == 0 || field.size > total_size || field.offset >= total_size
         || field.offset + field.size > total_size )
      return false;
    udm_t member;
    member.name = field.name.c_str();
    const char *scalar_decl = field.c_type == "__int64" ? "__int64 x;"
      : field.c_type == "int"                           ? "int x;"
      : field.c_type == "bool"                          ? "bool x;"
      : field.c_type == "float"                         ? "float x;"
      : field.c_type == "double"                        ? "double x;"
                                                        : nullptr;
    if ( scalar_decl != nullptr )
    {
      if ( !parse_decl(&member.type, nullptr, get_idati(), scalar_decl, PT_SIL)
           || member.type.empty() )
        return false;
    }
    else
    {
      // struct go__X / anything else: parse it (named types exist in the
      // til already). parse_decl needs a declarator + semicolon even under
      // PT_SIL, and it rejects the `struct NAME` spelling — "<bare name>
      // x;" is the accepted form.
      qstring decl;
      decl.sprnt("%s x;",
                 field.c_type.rfind("struct ", 0) == 0 ? field.c_type.c_str() + 7
                                                       : field.c_type.c_str());
      if ( !parse_decl(&member.type, nullptr, get_idati(), decl.c_str(), PT_SIL | PT_SEMICOLON)
           || member.type.empty() )
        return false; // fail-closed: the named field type is not in the til
    }
    member.offset = field.offset * 8; // udt offsets are in bits
    member.size = field.size * 8;
    udt.push_back(member);
  }
  tinfo_t named;
  if ( !named.create_udt(udt) || named.set_named_type(get_idati(), name, NTF_REPLACE) != TERR_OK )
    return false;
  // A failed save keeps the type resolvable by name through parse_decl,
  // which is all the prototype needs — report success only when the til
  // truly owns the definition.
  tinfo_t saved;
  return saved.get_named_type(get_idati(), name) && saved.get_size() == total_size;
}

bool make_wasm_function_type_uncached(const qvector<uchar> &params, const qvector<uchar> &results,
                                      bool indirect, tinfo_t *out)
{
  if ( results.size() <= 1 || (results.size() > 1 && wasm_results_size(results) > 16) )
    return make_parsed_wasm_function_type(params, results, indirect, out);
  const bool sret = results.size() > 1 && wasm_results_size(results) > 16;
  size_t words = 0;
  if ( results.size() > 1 && !multi_result_word_count(results, &words, sret) )
    return false;
  (void)words;

  func_type_data_t function;
  tinfo_t tuple_type;
  tinfo_t result_pointer_type;
  const auto type_decl = [](uchar type) -> const char *
  {
    return type == 0x7F                                              ? "int"
      : type == 0x7E                                                 ? "__int64"
      : type == 0x7D                                                 ? "float"
      : type == 0x7C                                                 ? "double"
      : type == 0x70 || type == 0x6F || type == 0x71 || type == 0x72 ? "unsigned int"
                                                                     : nullptr;
  };
  if ( results.size() <= 1 )
  {
    const char *result = results.empty() ? "void" : type_decl(results[0]);
    if ( result == nullptr )
      return false;
    qstring declaration(result);
    declaration.append(" __usercall wasmhex_checked_type");
    if ( !results.empty() && wasm_type_size(results[0]) == 8 )
      declaration.append("@<r0:r1>");
    else if ( !results.empty() )
      declaration.append("@<r0>");
    declaration.append("(");
    size_t word = 0;
    if ( indirect )
    {
      if ( !append_wasm_user_argument(&declaration, "unsigned int", "element_index", word, 4,
                                      false) )
        return false;
      word += 4;
    }
    for ( size_t i = 0; i < params.size(); ++i )
    {
      const char *type = type_decl(params[i]);
      qstring name;
      name.sprnt("a%zu", i + 1);
      const int size = params[i] == 0x7E || params[i] == 0x7C ? 8 : 4;
      if ( type == nullptr
           || !append_wasm_user_argument(&declaration, type, name.c_str(), word, size,
                                         indirect || i != 0) )
        return false;
      word += size_t(size);
    }
    if ( params.empty() && !indirect )
      declaration.append("void");
    declaration.append(");");
    return parse_decl(out, nullptr, get_idati(), declaration.c_str(), PT_SIL | PT_SEMICOLON);
  }
  if ( results.empty() )
  {
    if ( !get_wasm_tinfo(0, &function.rettype) )
    {
      if ( !parse_decl(&function.rettype, nullptr, get_idati(), "void", PT_SIL | PT_SEMICOLON) )
        return false;
    }
    function.retloc.set_stkoff(0);
  }
  else if ( results.size() == 1 )
  {
    const uchar type = results[0];
    if ( !get_wasm_tinfo(type, &function.rettype) )
      return false;
    const int size = wasm_type_size(type);
    if ( size == 4 )
      function.retloc.set_reg1(0);
    else if ( size == 8 )
      function.retloc.set_reg2(0, 1);
    else
      return false;
  }
  else if ( sret )
  {
    if ( !parse_decl(&function.rettype, nullptr, get_idati(), "void", PT_SIL | PT_SEMICOLON)
         || !parse_decl(&result_pointer_type, nullptr, get_idati(), "void *", PT_SEMICOLON) )
      return false;
    function.retloc.set_stkoff(0);
  }
  else
  {
    if ( !get_multi_result_type(results, &tuple_type)
         || !make_multi_return_location(results, &function.retloc) )
      return false;
    function.rettype = tuple_type;
  }

  function.set_cc(CM_CC_SPECIAL);
  function.flags |= FTI_ARGLOCS | FTI_EXPLOCS;
  const size_t count = params.size() + (indirect ? 1 : 0);
  size_t next_reg = 0;
  for ( size_t i = 0; i < count; ++i )
  {
    const uchar type = indirect && i == 0 ? 0x7F : params[i - (indirect ? 1 : 0)];
    tinfo_t param_type;
    if ( !get_wasm_tinfo(type, &param_type) )
      return false;
    const int size = int(param_type.get_size());
    argloc_t argloc;
    if ( !make_wasm_argument_location(next_reg, size, &argloc) )
      return false;
    qstring arg_name;
    arg_name.sprnt(indirect && i == 0 ? "element_index" : "a%zu", indirect ? i : i + 1);
    function.push_back(funcarg_t(arg_name.c_str(), param_type, argloc));
    next_reg += wasm_argument_word_count(next_reg, size);
  }
  if ( sret )
  {
    argloc_t result_location;
    if ( !make_wasm_argument_location(next_reg, 4, &result_location) )
      return false;
    function.push_back(funcarg_t("result", result_pointer_type, result_location));
    next_reg += wasm_argument_word_count(next_reg, 4);
  }
  for ( const funcarg_t &argument : function )
    if ( argument.argloc.is_stkoff() )
      function.stkargs
        = std::max(function.stkargs, uval_t(argument.argloc.stkoff() + argument.type.get_size()));
  return out->create_func(function);
  return true;
}

std::string make_wasm_function_type_key(const qvector<uchar> &params, const qvector<uchar> &results,
                                        bool indirect)
{
  std::string key;
  key.reserve(params.size() + results.size() + 3);
  key.append("P");
  for ( uchar type : params )
    key.push_back(char(type));
  key.append("R");
  for ( uchar type : results )
    key.push_back(char(type));
  key.push_back(indirect ? '1' : '0');
  return key;
}

void scan_wasm_function_type_cache(netnode &wasm_node)
{
  __attribute__((used)) static const char kKeepM4S1Marker[]
    = "WASMHEX_BUILD_MARKER_M4_S1_HONEST_PROTOTYPES_TYPE_CACHE_ICF";
  if ( kKeepM4S1Marker[0] == '\0' )
    return;
  wasm_function_type_cache_scanned = true;
  qvector<uchar> params;
  qvector<uchar> results;
  for ( int i = 0; i < get_func_qty(); ++i )
  {
    const ea_t fn_start = get_func_ea_by_num(size_t(i));
    if ( fn_start == BADADDR || !get_type_vector(wasm_node, fn_start, '4', &params)
         || !get_type_vector(wasm_node, fn_start, '5', &results) )
      continue;
    const std::string key = make_wasm_function_type_key(params, results, false);
    if ( wasm_function_type_cache.find(key) != wasm_function_type_cache.end() )
      continue;
    tinfo_t type;
    func_type_data_t fti;
    if ( !get_tinfo(&type, fn_start) || !type.is_func()
         || !type.get_func_details(&fti, GTD_CALC_ARGLOCS) )
      continue;
    // Never seed the shared (params, results) cache from a function's
    // C++-specific tinfo (which has struct pointer parameters and would
    // pollute every call site sharing the same wasm arity). Only seed from
    // pure wasm signatures (or sanitize each parameter back to its exact
    // wasm scalar type while preserving IDA's parsed arglocs/retloc).
    const bool sret = results.size() > 1 && wasm_results_size(results) > 16;
    if ( fti.size() != params.size() + size_t(sret) )
      continue;
    // go-sig-drop-pcb: a Step C window prototype whose PC_B selector was
    // dropped can now have EXACTLY the raw arity (params.size()) with its
    // FIRST argument at a stack argloc (`0:^off.size`) instead of the r0
    // register — seeding the shared per-arity cache from it would hand every
    // same-arity function a stack argloc where the filter's entry moves
    // expect r0 (MERR_BADARCH / HIDWORD regressions). Historical seeds all
    // carry the raw `a1@<r0>` first (register-located param0, later window
    // params may stay stack) — keep those, reject only the dropped form.
    if ( !params.empty() && !(fti[0].argloc.is_reg1() || fti[0].argloc.is_reg2()) )
      continue;
    bool ok = true;
    for ( size_t p = 0; p < params.size(); ++p )
    {
      if ( !fti[p].type.is_integral() && !fti[p].type.is_floating() )
      {
        tinfo_t pure_param;
        if ( !get_wasm_tinfo(params[p], &pure_param) )
        {
          ok = false;
          break;
        }
        fti[p].type = pure_param;
      }
      fti[p].name.sprnt("a%zu", p + 1);
    }
    if ( !ok )
      continue;
    tinfo_t clean_type;
    if ( clean_type.create_func(fti) )
      wasm_function_type_cache.emplace(key, clean_type);
  }
}

bool make_wasm_function_type(const qvector<uchar> &params, const qvector<uchar> &results,
                             bool indirect, tinfo_t *out)
{
  std::string key = make_wasm_function_type_key(params, results, indirect);
  const auto cached = wasm_function_type_cache.find(key);
  if ( cached != wasm_function_type_cache.end() )
  {
    *out = cached->second;
    return true;
  }
  if ( !wasm_function_type_cache_scanned )
  {
    netnode wasm_node(wasmhex_node_name, 0, false);
    if ( wasm_node != BADNODE )
      scan_wasm_function_type_cache(wasm_node);
    const auto scanned = wasm_function_type_cache.find(key);
    if ( scanned != wasm_function_type_cache.end() )
    {
      *out = scanned->second;
      return true;
    }
  }
  if ( !make_wasm_function_type_uncached(params, results, indirect, out) )
    return false;
  wasm_function_type_cache.emplace(std::move(key), *out);
  return true;
}

void clear_wasm_function_type_cache()
{
  wasm_function_type_cache.clear();
  wasm_function_type_cache_scanned = false;
}

#ifndef WASMHEX_NO_HEXRAYS
bool make_regloc_mop(codegen_t &cdg, const argloc_t &loc, int size, mop_t *out)
{
  if ( size == 4 && loc.is_reg1() )
  {
    out->make_reg(reg2mreg(loc.reg1()), size);
    return true;
  }
  if ( size == 8 && loc.is_reg2() )
  {
    out->make_reg_pair(reg2mreg(loc.reg1()), reg2mreg(loc.reg2()), 4);
    return true;
  }
  if ( (size == 4 || size == 8) && loc.is_stkoff() )
  {
    out->make_stkvar(cdg.mba, cdg.mba->inargoff + loc.stkoff());
    out->size = size;
    return true;
  }
  return false;
}
#endif // WASMHEX_NO_HEXRAYS

// Typed accessors (ida/metadata.hpp): every store prepends the version
// byte; the payload after it is byte-identical to the v0 layout so reads of
// not-yet-flipped slots keep working.

bool store_branch_targets(netnode &node, DatabaseEA key, const ea_t *targets, size_t count)
{
  if ( targets == nullptr || count == 0 )
    return false;
  bytevec_t blob;
  blob.resize_noinit(1 + count * sizeof(ea_t));
  blob[0] = kMetadataV1;
  memcpy(&blob[1], targets, count * sizeof(ea_t));
  return node.supset_ea(ea_t(key.v), blob.begin(), blob.size(), 'C');
}

bool load_branch_targets(DatabaseEA key, qvector<ea_t> *targets)
{
  return get_ea_list(ea_t(key.v), 'C', targets);
}

bool store_packed_branch_table(netnode &node, DatabaseEA key, const uint32_t *depths,
                               size_t depth_count, const ea_t *targets, size_t target_count)
{
  if ( targets == nullptr || target_count == 0 || target_count > UINT32_MAX )
    return false;
  const uint32 count = uint32(target_count);
  bytevec_t blob;
  blob.resize_noinit(1 + 4 + size_t(count) * (4 + sizeof(ea_t)));
  size_t at = 0;
  blob[at++] = kMetadataV1;
  for ( unsigned byte = 0; byte < 4; ++byte )
    blob[at++] = uchar(count >> (byte * 8));
  // One depth per target: a short scan-side list is zero-padded (KHAN fix
  // preserved), a longer one is truncated to the target count.
  for ( size_t i = 0; i < count; ++i )
  {
    const uint32 depth = i < depth_count ? depths[i] : 0;
    for ( unsigned byte = 0; byte < 4; ++byte )
      blob[at++] = uchar(depth >> (byte * 8));
  }
  for ( size_t i = 0; i < count; ++i )
    for ( unsigned byte = 0; byte < sizeof(ea_t); ++byte )
      blob[at++] = uchar(targets[i] >> (byte * 8));
  return node.setblob_ea(blob.begin(), blob.size(), ea_t(key.v), 'c');
}

bool load_packed_branch_table(DatabaseEA key, qvector<ea_t> *targets, qvector<uint32> *depths)
{
  return get_branch_table_metadata(ea_t(key.v), targets, depths);
}

bool store_branch_depths(netnode &node, DatabaseEA key, const uint32_t *depths, size_t count)
{
  if ( depths == nullptr || count == 0 )
    return false;
  bytevec_t blob;
  blob.resize_noinit(1 + count * sizeof(uint32));
  blob[0] = kMetadataV1;
  memcpy(&blob[1], depths, count * sizeof(uint32));
  return node.supset_ea(ea_t(key.v), blob.begin(), blob.size(), 'D');
}

bool load_branch_depths(DatabaseEA key, qvector<uint32> *depths)
{
  return get_u32_list(ea_t(key.v), 'D', depths);
}

bool store_control_types(netnode &node, DatabaseEA key, const uchar *types, size_t count)
{
  if ( types == nullptr && count != 0 )
    return false;
  bytevec_t blob;
  blob.push_back(kMetadataV1);
  for ( uint64 value = count;; )
  {
    const uint64 rest = value >> 7;
    blob.push_back(uchar((value & 0x7F) | (rest != 0 ? 0x80 : 0)));
    if ( rest == 0 )
      break;
    value = rest;
  }
  for ( size_t i = 0; i < count; ++i )
    blob.push_back(types[i]);
  return node.supset_ea(ea_t(key.v), blob.begin(), blob.size(), 'T');
}

bool load_control_types(DatabaseEA key, qvector<uchar> *types)
{
  netnode wasm_node(wasmhex_node_name, 0, false);
  return wasm_node != BADNODE && get_type_vector(wasm_node, ea_t(key.v), 'T', types);
}

bool store_edge_source(netnode &node, DatabaseEA key, ea_t source)
{
  bytevec_t blob;
  blob.resize_noinit(1 + sizeof(ea_t));
  blob[0] = kMetadataV1;
  memcpy(&blob[1], &source, sizeof(source));
  return node.supset_ea(ea_t(key.v), blob.begin(), blob.size(), 'S');
}

bool load_edge_source(DatabaseEA key, ea_t *source)
{
  return get_ea_value(ea_t(key.v), 'S', source);
}

bool store_dispatch_cases(netnode &node, DatabaseEA key, const uint32 *cases, size_t count)
{
  if ( cases == nullptr && count != 0 )
    return false;
  if ( count > WASMHEX_MAX_SPARSE_TABLE_DISPATCH_CASES )
    return false;
  // Wire: 0x01 | uint32 count | count * {slot, function, trap}.
  const size_t words = 1 + count * 3;
  bytevec_t blob;
  blob.resize_noinit(1 + words * sizeof(uint32));
  blob[0] = kMetadataV1;
  const auto put32 = [&blob](size_t at, uint32 value)
  {
    for ( unsigned byte = 0; byte < 4; ++byte )
      blob[at + byte] = uchar(value >> (byte * 8));
  };
  put32(1, uint32(count));
  for ( size_t i = 0; i < count * 3; ++i )
    put32(1 + sizeof(uint32) + i * sizeof(uint32), cases[i]);
  return node.setblob_ea(blob.begin(), blob.size(), ea_t(key.v), 'j') != 0;
}

bool read_dispatch_cases(netnode &wasm_node, ea_t key, qvector<uint32> *cases)
{
  if ( wasm_node == BADNODE || cases == nullptr )
    return false;
  bytevec_t encoded;
  if ( wasm_node.getblob_ea(&encoded, key, 'j') <= 0 )
    return false;
  const uchar *const data = encoded.begin();
  const size_t size = encoded.size();
  if ( size < 1 + sizeof(uint32) || data[0] != kMetadataV1 )
    return false;
  const size_t words = (size - 1) / sizeof(uint32);
  if ( (size - 1) % sizeof(uint32) != 0 || words < 1 )
    return false;
  qvector<uint32> blob;
  blob.resize(words);
  memcpy(blob.begin(), data + 1, words * sizeof(uint32));
  const size_t count = size_t(blob[0]);
  if ( count > WASMHEX_MAX_SPARSE_TABLE_DISPATCH_CASES || count * 3 + 1 != words )
    return false;
  for ( size_t k = 0; k < count; ++k )
    if ( blob[1 + k * 3 + 2] > uint32(wasmhex_call_trap_t::type_mismatch) )
      return false;
  // Callers get the flat triplets; the leading count word stays behind.
  cases->clear();
  cases->reserve(count * 3);
  for ( size_t i = 1; i <= count * 3; ++i )
    cases->push_back(blob[i]);
  return true;
}

bool store_go_module(netnode &node)
{
  const uchar blob[2] = { kMetadataV1, 1 };
  return node.supset(0, blob, sizeof(blob), 'G');
}

bool load_go_module()
{
  netnode wasm_node(wasmhex_node_name, 0, false);
  if ( wasm_node == BADNODE )
    return false;
  const ssize_t size = wasm_node.supval(0, nullptr, 0, 'G');
  if ( size == 2 ) // v1: [0x01][flag]
  {
    uchar blob[2] = {};
    if ( wasm_node.supval(0, blob, sizeof(blob), 'G') != 2 )
      return false;
    return blob[0] == kMetadataV1 && blob[1] == 1;
  }
  if ( size == 1 ) // v0: raw uchar set from the import name alone
  {
    uchar raw = 0;
    if ( wasm_node.supval(0, &raw, sizeof(raw), 'G') != 1 )
      return false;
    return raw == 1;
  }
  return false;
}

bool store_go_function_ir(netnode &node, DatabaseEA key, const loader::go_function_ir_t &ir)
{
  std::vector<uint32> blob;
  loader::encode_go_function_ir_blob(ir, &blob);
  if ( blob.empty() )
    return true;
  return node.setblob_ea(blob.data(), blob.size() * sizeof(blob[0]), ea_t(key.v), 'x') != 0;
}

bool load_go_function_ir(netnode &node, ea_t key, loader::go_function_ir_t *ir, bool *has_blob)
{
  if ( has_blob != nullptr )
    *has_blob = false;
  if ( node == BADNODE || ir == nullptr )
    return false;
  qvector<uint32> protocol;
  const ssize_t protocol_size = node.getblob_ea(&protocol, key, 'x');
  if ( protocol_size <= 0 )
  {
    *ir = loader::go_function_ir_t{};
    return false;
  }
  if ( has_blob != nullptr )
    *has_blob = true;
  return loader::decode_go_function_ir_blob(protocol.begin(), protocol.size(), ir);
}

// The ModuleIndex cache. The index
// decodes blob 't' (+ 'Z', + the 'F' import boundary) ONCE per IDB per
// process; every F5 after the first pays one getblob + hash for the
// identity check instead of the 3 full blob-'t' decodes the collectors
// used to run. Identity = FNV-1a over the raw source bytes + schema
// (per-IDB, never a plain process static); closebase clears
// the cache outright (clear_wasmhex_module_index) so a fresh database
// cannot inherit anything, and the identity check covers every other
// mutation path (a re-load rewrites the blobs; identical bytes keep the
// cache, which is correct because the decode is a pure function).
namespace
{
struct module_index_cache_t
{
  bool valid = false;
  std::uint64_t identity = 0;
  loader::go_module_index_t index;
};

module_index_cache_t &module_index_cache()
{
  static module_index_cache_t cache;
  return cache;
}

// Process-cumulative rebuild count (the measurement bar: 1 per identity
// per process is the goal; the IRTIME line prints the per-function delta).
unsigned &module_index_build_count()
{
  static unsigned count = 0;
  return count;
}

// The smallest live 'F' key (the function import count) — ported verbatim
// from the per-F5 probe collect_go_call_shapes used to run: binary search
// over the netnode index space, UINT32_MAX when no defined function exists.
std::uint32_t probe_import_count(netnode &wasm_node)
{
  size_t lo = 0;
  if ( wasm_node.eaget_idx(0, 'F') == BADADDR || wasm_node.eaget_idx(0, 'F') == 0 )
  {
    size_t hi = 1;
    while ( hi < (1u << 22) )
    {
      const ea_t probe = wasm_node.eaget_idx(uint32(hi), 'F');
      if ( probe != BADADDR && probe != 0 )
        break;
      hi *= 2;
    }
    if ( hi >= (1u << 22) )
      return UINT32_MAX;
    lo = hi / 2 + 1;
    while ( lo < hi )
    {
      const size_t mid = (lo + hi) / 2;
      const ea_t probe = wasm_node.eaget_idx(uint32(mid), 'F');
      if ( probe == BADADDR || probe == 0 )
        lo = mid + 1;
      else
        hi = mid;
    }
    if ( lo >= (1u << 22) )
      return UINT32_MAX;
  }
  return uint32(lo);
}
} // namespace

const loader::go_module_index_t *go_module_index(netnode &wasm_node)
{
  module_index_cache_t &cache = module_index_cache();
  if ( wasm_node == BADNODE )
    return nullptr;
  bytevec_t t_raw;
  const ssize_t t_size = wasm_node.getblob(&t_raw, 0, loader::go_types_blob_tag);
  bytevec_t z_raw;
  const ssize_t z_size = wasm_node.getblob(&z_raw, 0, 'Z');
  const std::uint32_t import_count = probe_import_count(wasm_node);
  const std::uint32_t *t_words = nullptr;
  std::size_t t_word_count = 0;
  if ( t_size > 0 && (size_t(t_size) % sizeof(std::uint32_t)) == 0 )
  {
    t_words = reinterpret_cast<const std::uint32_t *>(t_raw.begin());
    t_word_count = size_t(t_size) / sizeof(std::uint32_t);
  }
  const std::uint64_t identity = loader::go_module_identity(
    t_words, t_word_count, z_raw.begin(), z_size > 0 ? size_t(z_size) : 0, import_count,
    loader::kModuleIndexSchema);
  if ( cache.valid && cache.identity == identity )
    return &cache.index;
  loader::go_module_index_t index;
  loader::go_build_module_index(t_words, t_word_count, z_raw.begin(),
                                z_size > 0 ? size_t(z_size) : 0, import_count, &index);
  index.identity = identity;
  cache.valid = true;
  cache.identity = identity;
  cache.index = std::move(index); // in place: outstanding pointers stay valid
  ++module_index_build_count();
  return &cache.index;
}

void clear_wasmhex_module_index()
{
  module_index_cache_t &cache = module_index_cache();
  cache.valid = false;
  cache.identity = 0;
  cache.index = loader::go_module_index_t{};
}

unsigned go_module_index_builds()
{
  return module_index_build_count();
}

// The per-function FunctionIR cache (bounded LRU — a sweep
// decompiles each function once, so the cap bounds memory; an interactive
// re-F5 of the same function is the pure hit path). Entries live in a
// std::list so pointers stay stable across touches/evictions of OTHER
// entries; the filter re-fetches per reset_state and clears its pointer in
// clear_runtime_state, so no pointer outlives its function's decompile.
namespace
{
constexpr std::size_t kFunctionIrCacheEntries = 16;

struct function_ir_lru_t
{
  std::list<loader::function_ir_t> entries;
  std::unordered_map<ea_t, std::list<loader::function_ir_t>::iterator> by_entry;
  unsigned builds = 0;

  loader::function_ir_t *touch(ea_t entry)
  {
    const auto found = by_entry.find(entry);
    if ( found == by_entry.end() )
      return nullptr;
    entries.splice(entries.begin(), entries, found->second);
    found->second = entries.begin();
    return &*entries.begin();
  }

  loader::function_ir_t &insert(ea_t entry)
  {
    // A rebuild for an already-cached EA replaces its node outright (the
    // old one must not linger unreachable until capacity eviction).
    const auto existing = by_entry.find(entry);
    if ( existing != by_entry.end() )
    {
      entries.erase(existing->second);
      by_entry.erase(existing);
    }
    entries.push_front(loader::function_ir_t{});
    by_entry[entry] = entries.begin();
    while ( entries.size() > kFunctionIrCacheEntries )
    {
      // Drop the index-map binding of the evicted tail (the map is tiny,
      // a scan for the tail iterator is fine).
      const auto tail_it = std::prev(entries.end());
      for ( auto it = by_entry.begin(); it != by_entry.end(); ++it )
      {
        if ( it->second == tail_it )
        {
          by_entry.erase(it);
          break;
        }
      }
      entries.pop_back();
    }
    return entries.front();
  }
};

function_ir_lru_t &function_ir_lru()
{
  static function_ir_lru_t lru;
  return lru;
}
} // namespace

loader::function_ir_t *go_function_ir(netnode &wasm_node, ea_t entry)
{
  if ( wasm_node == BADNODE || entry == BADADDR )
    return nullptr;
  func_entry_info_t owner;
  if ( !get_func_entry_info(&owner, entry) || owner.size() == 0 )
    return nullptr;
  // Raw blob 'x' first: absent -> non-Go function, no cache work at all
  // (exactly the old load_go_function_ir fast bail).
  bytevec_t x_raw;
  const ssize_t x_size = wasm_node.getblob_ea(&x_raw, entry, 'x');
  if ( x_size <= 0 )
    return nullptr;
  // Body bytes fingerprint (user patches invalidate).
  std::vector<std::uint8_t> body(size_t(owner.size()));
  if ( get_bytes(body.data(), body.size(), owner.start_ea) != ssize_t(body.size()) )
    return nullptr;
  const loader::go_module_index_t *index = go_module_index(wasm_node);
  const std::uint64_t module_identity = index != nullptr ? index->identity : 0;
  const std::uint64_t body_hash = loader::go_hash64(body.data(), body.size(), module_identity);
  const std::uint64_t wire_hash = loader::go_hash64(x_raw.begin(), size_t(x_size), module_identity);
  loader::function_ir_t *cached = function_ir_lru().touch(entry);
  if ( cached != nullptr && cached->module_identity == module_identity
       && cached->body_hash == body_hash && cached->wire_hash == wire_hash )
    return cached;
  // Rebuild (any key mismatch: bytes, blob 'x', or module metadata).
  loader::function_ir_t &fresh = function_ir_lru().insert(entry);
  ++function_ir_lru().builds;
  fresh.module_identity = module_identity;
  fresh.body_hash = body_hash;
  fresh.wire_hash = wire_hash;
  fresh.start_ea = owner.start_ea;
  fresh.end_ea = owner.end_ea;
  fresh.wire_present = true;
  const std::uint32_t *x_words = reinterpret_cast<const std::uint32_t *>(x_raw.begin());
  const std::size_t x_word_count = size_t(x_size) / sizeof(std::uint32_t);
  fresh.wire_valid = (size_t(x_size) % sizeof(std::uint32_t)) == 0 && x_word_count > 0
    && loader::decode_go_function_ir_blob(x_words, x_word_count, &fresh.ir);
  // The ONE linear decode of the body.
  fresh.insns.clear();
  fresh.insns.reserve(size_t(owner.size()) / 2 + 8);
  fresh.stream_valid = true;
  for ( ea_t ea = owner.start_ea; ea < owner.end_ea; )
  {
    insn_t insn;
    const int size = decode_insn(&insn, ea);
    if ( size <= 0 )
    {
      fresh.stream_valid = false;
      fresh.insns.clear();
      break;
    }
    loader::go_insn_record_t record;
    record.ea = ea;
    record.itype = std::int32_t(insn.itype);
    record.op1 = insn.Op1.value;
    fresh.insns.push_back(record);
    ea += size;
  }
  return &fresh;
}

unsigned go_function_ir_builds()
{
  return function_ir_lru().builds;
}

void clear_wasmhex_function_ir_cache()
{
  function_ir_lru_t &lru = function_ir_lru();
  lru.entries.clear();
  lru.by_entry.clear();
}

bool store_cfg_reject(ea_t func_ea, wasmhex_cfg_reject_t reason, ea_t from, ea_t to)
{
  netnode wasm_node(wasmhex_node_name, 0, false);
  if ( wasm_node == BADNODE )
    return false;
  // Wire: 0x02 | reason | ea from | ea to (little endian, like every blob).
  bytevec_t blob;
  blob.push_back(0x02);
  blob.push_back(uchar(reason));
  const ea_t edge[2] = { from, to };
  for ( ea_t value : edge )
    for ( unsigned byte = 0; byte < sizeof(ea_t); ++byte )
      blob.push_back(uchar(uint64(value) >> (byte * 8)));
  return wasm_node.supset_ea(func_ea, blob.begin(), blob.size(), 'u');
}

const char *cfg_reject_reason(wasmhex_cfg_reject_t reason)
{
  switch ( reason )
  {
    case wasmhex_cfg_reject_t::out_of_range: return "target outside the 32-bit code segment";
    case wasmhex_cfg_reject_t::cross_function:
      return "cross-function target without Go dispatch provenance";
    case wasmhex_cfg_reject_t::not_boundary:
      return "target is not an instruction boundary of the linear decode";
    case wasmhex_cfg_reject_t::decode_failed:
      return "the owning function failed to decode linearly";
  }
  return "unknown rejection reason";
}
}
