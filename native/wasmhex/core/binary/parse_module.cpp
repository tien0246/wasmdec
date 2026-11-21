// The section-driver half of the old core/module.cpp — the
// top-level module_t::parse loop plus the limits/memory-type helpers the
// per-section parsers (sections.cpp) consume. Stays in WASMHEX_CORE_REST
// (not the wasmhex_binary library): it calls parse_code (which calls the
// analysis-side scan_control) and parse_pclntab (debuginfo), so putting it
// in the library would invert the binary <- analysis/debuginfo direction.
#include "../types.hpp"

#include <algorithm>
#include <cstring> // memcmp: declare directly, never via transitive includes

namespace wasmhex::loader
{
bool module_t::skip_limits(reader_t *r, uint64 *minimum, uint64 *flags_out, uint64 *maximum,
                           bool *has_maximum)
{
  uint64 flags, limit, minimum_value;
  if ( !r->uleb(&flags) )
    return false;
  if ( (flags & ~uint64(7)) != 0 )
  {
    reject(error_code::policy_other, "Unsupported WebAssembly table limits flags");
    return false;
  }
  if ( !r->uleb(&limit, (flags & 0x04) != 0 ? 64 : 32) )
    return false;
  minimum_value = limit;
  if ( minimum != nullptr )
    *minimum = limit;
  if ( flags_out != nullptr )
    *flags_out = flags;
  const bool bounded = (flags & 1) != 0;
  if ( has_maximum != nullptr )
    *has_maximum = bounded;
  if ( bounded )
  {
    if ( !r->uleb(&limit, (flags & 0x04) != 0 ? 64 : 32) )
      return false;
    if ( limit < minimum_value )
    {
      reject(error_code::bad_table, "WebAssembly limits maximum is below minimum");
      return false;
    }
    if ( maximum != nullptr )
      *maximum = limit;
  }
  else if ( maximum != nullptr )
    *maximum = 0;
  return true;
}

bool module_t::parse_memory_type(reader_t *r, bool imported)
{
  uint64 flags, minimum, maximum;
  if ( ++memory_count != 1 )
  {
    reject(error_code::policy_multiple_memories,
           "Native wasmhex supports one memory at index zero; multiple memories are unsupported");
    return false;
  }
  if ( !r->uleb(&flags) || (flags & ~uint64(7)) != 0 )
    return false;
  const bool has_maximum = (flags & 1) != 0;
  const bool shared = (flags & 2) != 0;
  const bool is_memory64 = (flags & 4) != 0;
  const unsigned bits = is_memory64 ? 64 : 32;
  const uint64 page_limit = is_memory64 ? (uint64(1) << 48) : (uint64(1) << 16);
  if ( (shared && !has_maximum) || !r->uleb(&minimum, bits) )
    return false;
  if ( minimum > page_limit )
  {
    if ( is_memory64 )
      reject(error_code::policy_memory64, "Native wasmhex rejects Memory64 sizes above 2^48 pages");
    else
      reject(error_code::policy_stack_limits,
             "Native wasmhex rejects Memory32 sizes above 2^16 pages");
    return false;
  }
  if ( has_maximum )
  {
    if ( !r->uleb(&maximum, bits) )
      return false;
    if ( maximum > page_limit )
    {
      if ( is_memory64 )
        reject(error_code::policy_memory64,
               "Native wasmhex rejects Memory64 sizes above 2^48 pages");
      else
        reject(error_code::policy_stack_limits,
               "Native wasmhex rejects Memory32 sizes above 2^16 pages");
      return false;
    }
    if ( maximum < minimum )
      return false;
  }
  memory64 = is_memory64;
  memory_shared = shared;
  memory_imported = imported;
  memory_minimum_pages = minimum;
  memory_has_declared_maximum = has_maximum;
  memory_declared_maximum_pages = has_maximum ? maximum : 0;
  return true;
}

bool module_t::parse(const uchar *data, size_t size)
{
  static const uchar magic[] = { 0x00, 'a', 's', 'm', 0x01, 0x00, 0x00, 0x00 };
  if ( size < sizeof(magic) || memcmp(data, magic, sizeof(magic)) != 0 )
    return false;

  reader_t r = { data, size, sizeof(magic) };
  uint8 last_order = 0;
  while ( r.pos < r.size )
  {
    uchar id;
    uint64 section_size;
    if ( !r.byte(&id) || !r.uleb(&section_size) || section_size > r.size - r.pos )
      return false;
    failure_section_id = id;
    if ( id != 0 )
    {
      const uint8 order = section_order(id);
      if ( order == 0 || order <= last_order )
        return false;
      last_order = order;
      const uint16 mask = uint16(1) << id;
      if ( (seen_sections & mask) != 0 )
        return false;
      seen_sections |= mask;
    }
    reader_t section = { r.data + r.pos, size_t(section_size) };
    r.pos += size_t(section_size);
    bool ok = true;
    switch ( id )
    {
      case 0: ok = parse_custom(&section); break;
      case 1: ok = parse_types(&section); break;
      case 2: ok = parse_imports(&section); break;
      case 3: ok = parse_functions(&section); break;
      case 4: ok = parse_tables(&section); break;
      case 13: ok = parse_tags(&section); break;
      case 7: ok = parse_exports(&section); break;
      case 8: ok = parse_start(&section); break;
      case 6: ok = parse_globals(&section); break;
      case 9: ok = parse_elements(&section); break;
      // Go modules embed their pclntab in the data payloads; recover the
      // function names now that every active segment has been collected.
      // Runs here in the driver (not in parse_data/sections.cpp) because
      // pclntab is debuginfo: a wasmhex_binary -> wasmhex_debuginfo archive
      // reference breaks GNU ld's single-pass link order (Apple ld rescans
      // archives, which hid this until the first Linux CI run).
      case 11:
        ok = parse_data(&section);
        if ( ok )
          ok = parse_pclntab();
        break;
      case 12: ok = parse_data_count(&section); break;
      case 5: ok = parse_memories(&section); break;
      case 10: ok = parse_code(&section); break;
      default: break;
    }
    if ( !ok )
      return false;
  }
  if ( defined_func_types.size() != bodies.size() )
    return false;
  if ( function_imports.size() != imported_func_types.size() )
    return false;
  if ( (has_data_count && data_segment_sizes.size() != declared_data_count)
       || (uses_data_segment_ops && !has_data_count) )
  {
    if ( uses_data_segment_ops )
      reject(error_code::bad_data_count,
             "memory.init/data.drop requires a matching WebAssembly data count section");
    else
      reject(error_code::bad_data_count, "WebAssembly data count does not match the data section");
    return false;
  }
  for ( size_t i = 0; i < imported_func_types.size(); ++i )
    if ( imported_func_types[i] >= types.size() )
      return false;
  for ( size_t i = 0; i < defined_func_types.size(); ++i )
    if ( defined_func_types[i] >= types.size() )
      return false;
  const bool fixed_extent
    = (!memory_shared && !memory_imported && !memory_exported && !uses_memory_grow)
    || (memory_has_declared_maximum && memory_declared_maximum_pages == memory_minimum_pages);
  fixed_memory32 = memory_count == 1 && !memory64 && fixed_extent;
  fixed_memory64 = memory_count == 1 && memory64 && fixed_extent;
  // A memory32 without a maximum is still bounded by its 32-bit address space.
  growable_memory_pages
    = memory_has_declared_maximum ? memory_declared_maximum_pages : max_growable_memory_pages;
  growable_memory32 = memory_count == 1 && !memory64 && !memory_shared
    && (memory_imported || memory_exported || uses_memory_grow)
    && growable_memory_pages > memory_minimum_pages
    && growable_memory_pages <= max_growable_memory_pages;
  growable_memory64 = memory_count == 1 && memory64 && !memory_shared && !memory_imported
    && !memory_exported && uses_memory_grow && memory_has_declared_maximum
    && memory_declared_maximum_pages > memory_minimum_pages
    && memory_declared_maximum_pages <= max_growable_memory_pages;
  if ( fixed_memory32 )
    fixed_memory_bytes = memory_minimum_pages * 65536;
  else if ( fixed_memory64 )
    fixed_memory_bytes
      = memory_minimum_pages == (uint64(1) << 48) ? UINT64_MAX : memory_minimum_pages * 65536;
  if ( uses_i31 && (has_generic_eqref || has_externref) )
  {
    reject(
      error_code::policy_i31_constraints,
      "Native wasmhex i31 handles require a module without externref or other eqref-family values");
    return false;
  }
  auto signature_uses_i31 = [&](uint32 type_index)
  {
    const wasm_type_t &type = types[type_index];
    return std::find(type.params.begin(), type.params.end(), 0x72) != type.params.end()
      || std::find(type.results.begin(), type.results.end(), 0x72) != type.results.end();
  };
  for ( uint32 type_index = 0; type_index < types.size(); ++type_index )
    if ( signature_uses_i31(type_index) )
    {
      reject(
        error_code::policy_i31_constraints,
        "Native wasmhex i31 references are limited to function bodies, not function signatures");
      return false;
    }
  // Classify the toolchain AFTER every section parsed — the
  // producers custom section can trail the code section (main.wasm).
  resolve_go_profile();
  // Build the Go function IR once at module load time now that the
  // Go profile is resolved and verified (legacy 1.19 and unknown profiles
  // skip IR creation).
  build_go_function_irs(*this);
  build_eh_function_irs(*this);
  // Build the libc++ idiom evidence (SSO string dtor sites).
  // Fail-closed: modules without a "free" import/export keep empty IRs.
  build_libcxx_function_irs(*this);
  // The single-source param/result layout per defined body
  // (descriptor > arginfo > evidence, confidence-tagged) — both the loader
  // prototype path and the decompiler call-site path read this record via
  // module.go_types.layouts / the blob 't' v2 tail. The name callback
  // mirrors entry.cpp's function_name sequence exactly (name section ->
  // pclntab -> export, dedup accumulating in defined-body order) so the
  // record's anchors and the prototype path see identical names. No type
  // layer, no records (stripped modules stay exactly as before).
  if ( go_types.present )
  {
    std::unordered_set<std::string> layout_used_names;
    go_build_func_layouts(*this,
                          [this, &layout_used_names](std::size_t body_index) -> std::string
                          {
                            const std::string pclntab = body_index < pclntab_function_names.size()
                              ? pclntab_function_names[body_index]
                              : std::string();
                            return function_name(uint32(imported_func_types.size() + body_index),
                                                 function_exports, function_names,
                                                 &layout_used_names,
                                                 pclntab.empty() ? nullptr : &pclntab);
                          });
  }
  return true;
}
} // namespace wasmhex::loader
