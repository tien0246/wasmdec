#include "internal.hpp"

#include "../../core/debuginfo/dwarf.hpp"

#include <algorithm>
#include <cstring>
#include <map>
#include <set>
#include <vector>

namespace wasmhex::loader
{
namespace
{

// Scans an external .debug.wasm module and fills the still-empty section
// slots. The loaded buffers stay alive in `keep_alive`.
void load_external_debug_module(const char *path, dwarf_sections_t *slots,
                                std::vector<std::vector<uchar>> *keep_alive)
{
  FILE *file = qfopen(path, "rb");
  if ( file == nullptr )
  {
    msg("wasmhex: external_debug_info file '%s' not found (ignored)\n", path);
    return;
  }
  const int64 length = qfsize(file);
  if ( length < 8 || uint64(length) > max_module_size )
  {
    msg("wasmhex: external_debug_info file '%s' has an invalid size (ignored)\n", path);
    qfclose(file);
    return;
  }
  keep_alive->push_back(std::vector<uchar>());
  std::vector<uchar> &buffer = keep_alive->back();
  buffer.resize(size_t(length));
  const ssize_t read = qfread(file, buffer.data(), buffer.size());
  qfclose(file);
  if ( read != ssize_t(buffer.size()) )
  {
    msg("wasmhex: cannot read external_debug_info file '%s' (ignored)\n", path);
    keep_alive->pop_back();
    return;
  }
  static const uchar header[] = { 0x00, 'a', 's', 'm', 0x01, 0x00, 0x00, 0x00 };
  if ( std::memcmp(buffer.data(), header, sizeof(header)) != 0 )
  {
    msg("wasmhex: external_debug_info file '%s' is not a wasm module (ignored)\n", path);
    keep_alive->pop_back();
    return;
  }
  size_t pos = sizeof(header);
  size_t captured = 0;
  while ( pos < buffer.size() )
  {
    const uchar id = buffer[pos++];
    uint64 section_size;
    if ( !section_uleb_size(buffer.data(), buffer.size(), &pos, &section_size)
         || section_size > buffer.size() - pos )
      break;
    if ( id == 0 )
    {
      // Custom section: <name-len uleb> <name> <payload>.
      uint64 name_size;
      size_t inner = pos;
      if ( !section_uleb_size(buffer.data(), buffer.size(), &inner, &name_size)
           || name_size > section_size || name_size > 64 )
        break;
      const char *name = reinterpret_cast<const char *>(buffer.data() + inner);
      const size_t header_bytes = inner - pos;
      if ( header_bytes + name_size > section_size || name_size > 64 )
        break;
      const size_t payload = inner + size_t(name_size);
      const size_t payload_size = size_t(section_size - header_bytes - name_size);
      if ( payload > buffer.size() || payload_size > buffer.size() - payload )
        break;
      if ( name_size > 7 && std::memcmp(name, ".debug_", 7) == 0 )
      {
        const std::string tail(name + 7, size_t(name_size) - 7);
        dwarf_section_ref_t *slot = tail == "abbrev" ? &slots->abbrev
          : tail == "info"                           ? &slots->info
          : tail == "str"                            ? &slots->str
          : tail == "str_offsets"                    ? &slots->str_offsets
          : tail == "addr"                           ? &slots->addr
                                                     : nullptr;
        if ( slot != nullptr && slot->data == nullptr )
        {
          slot->data = buffer.data() + payload;
          slot->size = payload_size;
          ++captured;
        }
      }
    }
    pos += size_t(section_size);
  }
  msg("wasmhex: external_debug_info '%s' provided %u DWARF section(s)\n", path, unsigned(captured));
}

} // anonymous namespace

// Entry point: dwarf_apply_names
bool dwarf_apply_names(module_t *module, const char *input_file_path)
{
  if ( module == nullptr )
    return true;
  std::vector<std::vector<uchar>> keep_alive;
  keep_alive.reserve(8);
  if ( !module->dwarf.external_paths.empty() && input_file_path != nullptr
       && input_file_path[0] != '\0' )
  {
    std::string directory(input_file_path);
    const ssize_t slash = directory.rfind('/');
    if ( slash > 0 )
      directory = directory.substr(0, size_t(slash));
    else
      directory.clear();
    for ( size_t i = 0; i < module->dwarf.external_paths.size(); ++i )
    {
      const std::string &relative = module->dwarf.external_paths[i];
      if ( relative.empty() || relative.c_str()[0] == '/' )
        continue; // absolute paths are rejected (spec: module-relative)
      const std::string full = directory.empty() ? relative : directory + "/" + relative;
      load_external_debug_module(full.c_str(), &module->dwarf, &keep_alive);
    }
  }

  if ( !module->dwarf.has_debug_info() )
    return true; // no DWARF: zero cost, naming unchanged

  const dwarf_sections_ref_t sections = {
    &module->dwarf.str,
    &module->dwarf.str_offsets,
    &module->dwarf.addr,
  };
  const dwarf_section_ref_t &info = module->dwarf.info;
  const dwarf_section_ref_t &abbrev = module->dwarf.abbrev;

  dwarf_results_t results;
  bool saw_dwarf64 = false;
  parse_dwarf_units(info, abbrev, sections, &results, &saw_dwarf64);
  if ( saw_dwarf64 )
    msg("wasmhex: DWARF64 debug information is not supported (ignored)\n");

  // Map function bodies: code-section payload ranges (source_offset..source_end).
  struct body_range_t
  {
    uint32 start;
    uint32 end;
    size_t body_index;
  };
  std::vector<body_range_t> ranges;
  ranges.reserve(module->bodies.size());
  for ( size_t i = 0; i < module->bodies.size(); ++i )
    if ( module->bodies[i].source_end > module->bodies[i].source_offset )
      ranges.push_back({ module->bodies[i].source_offset, module->bodies[i].source_end, i });
  std::sort(ranges.begin(), ranges.end(),
            [](const body_range_t &a, const body_range_t &b) { return a.start < b.start; });
  auto find_body = [&](uint64 low_pc, size_t *body_index) -> bool
  {
    if ( ranges.empty() || low_pc > UINT32_MAX )
      return false;
    size_t lo = 0, hi = ranges.size();
    while ( lo < hi )
    {
      const size_t mid = lo + (hi - lo) / 2;
      if ( ranges[mid].start <= low_pc )
        lo = mid + 1;
      else
        hi = mid;
    }
    if ( lo == 0 )
      return false;
    const body_range_t &candidate = ranges[lo - 1];
    if ( low_pc >= candidate.start && low_pc < candidate.end )
    {
      *body_index = candidate.body_index;
      return true;
    }
    return false; // outside any function (e.g. linker-synthesized padding)
  };

  // Existing entries (fill-empty-first policy).
  std::set<uint32> named_functions;
  for ( size_t i = 0; i < module->function_names.size(); ++i )
    named_functions.insert(module->function_names[i].index);
  std::set<uint32> named_locals;
  for ( size_t i = 0; i < module->local_names.size(); ++i )
    named_locals.insert(module->local_names[i].function_index);
  std::set<uint32> named_segments;
  for ( size_t i = 0; i < module->data_segment_names.size(); ++i )
    named_segments.insert(module->data_segment_names[i].index);

  size_t applied_functions = 0;
  size_t applied_locals = 0;
  for ( size_t i = 0; i < results.subprograms.size(); ++i )
  {
    const dwarf_subprogram_t &sp = results.subprograms[i];
    if ( !sp.has_low_pc || sp.declaration_only || !sp.has_name )
      continue;
    size_t body_index = 0;
    if ( !find_body(sp.low_pc, &body_index) )
      continue;
    const uint32 function_index = uint32(module->imported_func_types.size() + body_index);
    if ( named_functions.find(function_index) == named_functions.end() )
    {
      module->function_names.emplace_back();
      export_t &entry = module->function_names.back();
      entry.name = sp.name;
      entry.index = function_index;
      named_functions.insert(function_index);
      ++applied_functions;
    }
    if ( sp.entries.empty() || named_locals.find(function_index) != named_locals.end() )
      continue;
    const wasm_type_t &signature = module->types[module->defined_func_types[body_index]];
    const size_t parameter_count = signature.params.size();
    const size_t local_count = parameter_count + module->bodies[body_index].local_types.size();
    function_local_names_t names;
    names.function_index = function_index;
    size_t parameter_ordinal = 0;
    size_t variable_ordinal = 0;
    for ( size_t e = 0; e < sp.entries.size(); ++e )
    {
      const dwarf_local_entry_t &entry = sp.entries[e];
      int64 local_index = -1;
      if ( entry.is_parameter )
      {
        // Declaration order wins; DW_OP_WASM_location confirms when present.
        local_index = parameter_ordinal < parameter_count ? int64(parameter_ordinal) : -1;
        ++parameter_ordinal;
        if ( entry.wasm_local >= 0 && entry.wasm_local < int64(parameter_count) )
          local_index = entry.wasm_local;
      }
      else
      {
        local_index = int64(parameter_count + variable_ordinal);
        ++variable_ordinal;
        if ( entry.wasm_local >= int64(parameter_count) )
          local_index = entry.wasm_local;
      }
      if ( !entry.has_name || local_index < 0 || local_index >= int64(local_count) )
        continue;
      local_name_entry_t local;
      local.local_index = uint32(local_index);
      local.name = entry.name;
      names.locals.push_back(local);
    }
    if ( !names.locals.empty() )
    {
      module->local_names.push_back(names);
      named_locals.insert(function_index);
      applied_locals++;
    }
  }

  size_t applied_segments = 0;
  for ( size_t i = 0; i < results.data_variables.size(); ++i )
  {
    const dwarf_data_variable_t &variable = results.data_variables[i];
    for ( size_t j = 0; j < module->data_segment_sizes.size(); ++j )
    {
      if ( j < module->data_segment_offsets.size() && module->data_segment_offsets[j] < 0 )
        continue; // passive or dynamic-offset segment
      const uint64 start
        = j < module->data_segment_offsets.size() ? uint64(module->data_segment_offsets[j]) : 0;
      if ( variable.address >= start && variable.address < start + module->data_segment_sizes[j]
           && named_segments.find(uint32(j)) == named_segments.end() )
      {
        module->data_segment_names.emplace_back();
        export_t &entry = module->data_segment_names.back();
        entry.name = variable.name;
        entry.index = uint32(j);
        named_segments.insert(uint32(j));
        ++applied_segments;
        break;
      }
    }
  }

  msg("wasmhex: DWARF names applied: %u function(s), %u function local set(s), "
      "%u data segment(s); skipped %u compile unit(s)\n",
      unsigned(applied_functions), unsigned(applied_locals), unsigned(applied_segments),
      unsigned(results.skipped_units));
  return true;
}

} // namespace wasmhex::loader
