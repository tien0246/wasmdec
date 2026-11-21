#include "../types.hpp"

#include <algorithm>
#include <cstring>

namespace wasmhex::loader
{

bool module_t::parse_custom(reader_t *r)
{
  std::string custom_name;
  if ( !r->name(&custom_name) )
    return false;
  if ( custom_name == "external_debug_info" )
  {
    // Tool-conventions "external debug info" section: vec(path) pointing at
    // a sibling .debug.wasm file that holds the DWARF sections. Paths are
    // resolved later (see dwarf_apply_names); missing files stay best-effort.
    uint64 count;
    if ( r->vector_count(&count) && count <= 8 )
    {
      for ( uint64 i = 0; i < count; ++i )
      {
        std::string path;
        if ( !r->name(&path) || path.empty() || path.length() > 1024 )
          break;
        dwarf.external_paths.push_back(path);
      }
    }
    return true;
  }
  if ( custom_name == "producers" )
  {
    // Tool-conventions producers section. wasmhex only records whether the
    // module was produced by the Go toolchain — Go modules normally carry
    // an embedded pclntab with every function name, so its absence means
    // the build pipeline stripped them (the Go linker itself never does).
    uint64 fields;
    if ( r->vector_count(&fields) && fields <= 64 )
    {
      for ( uint64 i = 0; i < fields; ++i )
      {
        std::string field;
        uint64 values;
        if ( !r->name(&field) || !r->vector_count(&values) || values > 64 )
          break;
        for ( uint64 v = 0; v < values; ++v )
        {
          // Producers entries are (name, version) PAIRS per the tool
          // conventions — the old reader consumed one string per entry and
          // silently stopped early (the boolean survived because "Go" is
          // the first name).
          std::string value;
          if ( !r->name(&value) )
            break;
          std::string version;
          if ( !r->name(&version) )
            break;
          if ( field == "language" && (value == "Go" || version.compare(0, 4, "go1.") == 0) )
            producers_go = true;
          // Layer 2: the processed-by version pins the family
          // ("Go" / "go1.27.3").
          if ( field == "processed-by" && go_profile.producers_version.empty()
               && version.compare(0, 4, "go1.") == 0 )
            go_profile.producers_version = version;
        }
      }
    }
    return true;
  }
  if ( custom_name.length() > 7 && custom_name.c_str()[0] == '.'
       && strncmp(custom_name.c_str(), ".debug_", 7) == 0 )
  {
    // Capture the DWARF sections the minimal parser understands. The reader
    // views the live module buffer, which outlives name application.
    const std::string tail = custom_name.substr(7);
    dwarf_section_ref_t *slot = tail == "abbrev" ? &dwarf.abbrev
      : tail == "info"                           ? &dwarf.info
      : tail == "str"                            ? &dwarf.str
      : tail == "str_offsets"                    ? &dwarf.str_offsets
      : tail == "addr"                           ? &dwarf.addr
                                                 : nullptr;
    if ( slot != nullptr && slot->data == nullptr )
    {
      slot->data = r->data + r->pos;
      slot->size = r->size - r->pos;
    }
    return true;
  }
  if ( custom_name != "name" )
    return true;

  std::vector<export_t> parsed_func_names;
  std::vector<export_t> parsed_global_names;
  std::vector<export_t> parsed_data_names;
  std::vector<function_local_names_t> parsed_local_names;
  while ( r->pos < r->size )
  {
    uchar subsection_id;
    uint64 subsection_size;
    if ( !r->byte(&subsection_id) || !r->uleb(&subsection_size)
         || subsection_size > r->size - r->pos )
      return true;
    reader_t subsection = { r->data + r->pos, size_t(subsection_size) };
    r->pos += size_t(subsection_size);
    if ( subsection_id == 1 || subsection_id == 7 || subsection_id == 9 )
    {
      uint64 count;
      if ( !subsection.vector_count(&count) )
        return true;
      std::vector<export_t> entries;
      for ( uint64 i = 0; i < count; ++i )
      {
        uint64 index;
        std::string name;
        if ( !subsection.uleb(&index) || index > UINT32_MAX || !subsection.name(&name) )
          return true;
        bool duplicate = false;
        for ( size_t j = 0; j < entries.size(); ++j )
          duplicate |= entries[j].index == index;
        if ( !duplicate )
        {
          entries.emplace_back();
          export_t &entry = entries.back();
          entry.name = name;
          entry.index = uint32(index);
        }
      }
      if ( subsection.pos != subsection.size )
        return true;
      std::vector<export_t> &target_list = subsection_id == 1 ? parsed_func_names
        : subsection_id == 7                                  ? parsed_global_names
                                                              : parsed_data_names;
      for ( size_t i = 0; i < entries.size(); ++i )
      {
        bool duplicate = false;
        for ( size_t j = 0; j < target_list.size(); ++j )
          duplicate |= target_list[j].index == entries[i].index;
        if ( !duplicate )
          target_list.push_back(entries[i]);
      }
    }
    else if ( subsection_id == 2 )
    {
      uint64 func_count;
      if ( !subsection.vector_count(&func_count) )
        return true;
      for ( uint64 f = 0; f < func_count; ++f )
      {
        uint64 func_index;
        uint64 num_locals;
        if ( !subsection.uleb(&func_index) || func_index > UINT32_MAX
             || !subsection.vector_count(&num_locals) )
          return true;
        function_local_names_t fn_locals;
        fn_locals.function_index = uint32(func_index);
        for ( uint64 l = 0; l < num_locals; ++l )
        {
          uint64 local_idx;
          std::string loc_name;
          if ( !subsection.uleb(&local_idx) || local_idx > UINT32_MAX
               || !subsection.name(&loc_name) )
            return true;
          local_name_entry_t entry;
          entry.local_index = uint32(local_idx);
          entry.name = loc_name;
          fn_locals.locals.push_back(entry);
        }
        parsed_local_names.push_back(fn_locals);
      }
    }
  }

  for ( size_t i = 0; i < parsed_func_names.size(); ++i )
  {
    bool duplicate = false;
    for ( size_t j = 0; j < function_names.size(); ++j )
      duplicate |= function_names[j].index == parsed_func_names[i].index;
    if ( !duplicate )
      function_names.push_back(parsed_func_names[i]);
  }
  for ( size_t i = 0; i < parsed_global_names.size(); ++i )
  {
    bool duplicate = false;
    for ( size_t j = 0; j < global_names.size(); ++j )
      duplicate |= global_names[j].index == parsed_global_names[i].index;
    if ( !duplicate )
      global_names.push_back(parsed_global_names[i]);
  }
  for ( size_t i = 0; i < parsed_data_names.size(); ++i )
  {
    bool duplicate = false;
    for ( size_t j = 0; j < data_segment_names.size(); ++j )
      duplicate |= data_segment_names[j].index == parsed_data_names[i].index;
    if ( !duplicate )
      data_segment_names.push_back(parsed_data_names[i]);
  }
  for ( size_t i = 0; i < parsed_local_names.size(); ++i )
  {
    bool duplicate = false;
    for ( size_t j = 0; j < local_names.size(); ++j )
      duplicate |= local_names[j].function_index == parsed_local_names[i].function_index;
    if ( !duplicate )
      local_names.push_back(parsed_local_names[i]);
  }
  return true;
}
} // namespace wasmhex::loader
