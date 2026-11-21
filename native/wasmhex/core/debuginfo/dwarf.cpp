// The SDK-free half of the DWARF name recovery — section
// readers, abbrev tables, form decoding and the compile-unit walk. Results
// are collected into dwarf_results_t; the fill-empty-first merge into
// module_t and all diagnostics live in the loader entry point.
#include "dwarf.hpp"

#include <algorithm>
#include <cstring>
#include <map>
#include <set>
#include <vector>

namespace wasmhex::loader
{
namespace
{

// DWARF constants (subset used here)
enum : uint32
{
  DW_TAG_array_type = 0x01,
  DW_TAG_enumeration_type = 0x04,
  DW_TAG_formal_parameter = 0x05,
  DW_TAG_lexical_block = 0x0b,
  DW_TAG_member = 0x0d,
  DW_TAG_pointer_type = 0x0f,
  DW_TAG_compile_unit = 0x11,
  DW_TAG_structure_type = 0x13,
  DW_TAG_subroutine_type = 0x15,
  DW_TAG_typedef = 0x16,
  DW_TAG_union_type = 0x17,
  DW_TAG_inlined_subroutine = 0x1d,
  DW_TAG_subprogram = 0x2e,
  DW_TAG_base_type = 0x24,
  DW_TAG_variable = 0x34,
};

enum : uint32
{
  DW_AT_location = 0x02,
  DW_AT_name = 0x03,
  DW_AT_low_pc = 0x11,
  DW_AT_abstract_origin = 0x31,
  DW_AT_declaration = 0x3c,
  DW_AT_specification = 0x47,
  DW_AT_str_offsets_base = 0x72,
  DW_AT_addr_base = 0x73,
};

enum : uint32
{
  DW_FORM_addr = 0x01,
  DW_FORM_block2 = 0x03,
  DW_FORM_block4 = 0x04,
  DW_FORM_data2 = 0x05,
  DW_FORM_data4 = 0x06,
  DW_FORM_data8 = 0x07,
  DW_FORM_string = 0x08,
  DW_FORM_block = 0x09,
  DW_FORM_block1 = 0x0a,
  DW_FORM_data1 = 0x0b,
  DW_FORM_flag = 0x0c,
  DW_FORM_sdata = 0x0d,
  DW_FORM_strp = 0x0e,
  DW_FORM_udata = 0x0f,
  DW_FORM_ref1 = 0x11,
  DW_FORM_ref2 = 0x12,
  DW_FORM_ref4 = 0x13,
  DW_FORM_ref8 = 0x14,
  DW_FORM_ref_udata = 0x15,
  DW_FORM_indirect = 0x16,
  DW_FORM_sec_offset = 0x17,
  DW_FORM_exprloc = 0x18,
  DW_FORM_flag_present = 0x19,
  DW_FORM_strx = 0x1a,
  DW_FORM_addrx = 0x1b,
  DW_FORM_ref_sup4 = 0x1c,
  DW_FORM_strp_sup = 0x1d,
  DW_FORM_data16 = 0x1e,
  DW_FORM_line_strp = 0x1f,
  DW_FORM_ref_sig8 = 0x20,
  DW_FORM_implicit_const = 0x21,
  DW_FORM_loclistx = 0x22,
  DW_FORM_rnglistx = 0x23,
  DW_FORM_ref_sup8 = 0x24,
  DW_FORM_strx1 = 0x25,
  DW_FORM_strx2 = 0x26,
  DW_FORM_strx3 = 0x27,
  DW_FORM_strx4 = 0x28,
  DW_FORM_addrx1 = 0x29,
  DW_FORM_addrx2 = 0x2a,
  DW_FORM_addrx3 = 0x2b,
  DW_FORM_addrx4 = 0x2c,
};

enum : uchar
{
  DW_OP_addr = 0x03,
  // LLVM wasm extension: uleb kind (0 local, 1 global, 2 stack) + uleb index
  DW_OP_WASM_location = 0xee,
};

enum : uint32
{
  DW_UT_compile = 0x01,
  DW_UT_partial = 0x03,
  DW_UT_skeleton = 0x04,
  DW_UT_split_compile = 0x05,
};

constexpr size_t dwarf_max_dies = 3000000;
constexpr size_t dwarf_max_units = 65536;
constexpr size_t dwarf_max_stack = 96;

// Small reader over a captured section
struct dwarf_reader_t
{
  const uchar *data = nullptr;
  size_t size = 0;
  size_t pos = 0;

  bool uleb(uint64 *out)
  {
    uint64 value = 0;
    unsigned shift = 0;
    for ( unsigned i = 0; i < 10; ++i )
    {
      if ( pos >= size )
        return false;
      const uchar b = data[pos++];
      const uint64 part = b & 0x7F;
      if ( shift >= 64 || part > (UINT64_MAX >> shift) )
        return false;
      value |= part << shift;
      if ( (b & 0x80) == 0 )
      {
        *out = value;
        return true;
      }
      shift += 7;
    }
    return false;
  }

  bool sleb(int64 *out)
  {
    uint64 value = 0;
    unsigned shift = 0;
    for ( unsigned i = 0; i < 10; ++i )
    {
      if ( pos >= size )
        return false;
      const uchar b = data[pos++];
      value |= uint64(b & 0x7F) << shift;
      shift += 7;
      if ( (b & 0x80) == 0 )
      {
        if ( (b & 0x40) != 0 && shift < 64 )
          value |= (~uint64(0)) << shift;
        *out = int64(value);
        return true;
      }
    }
    return false;
  }

  bool fixed(uint64 *out, size_t bytes)
  {
    if ( bytes == 0 || bytes > 16 || bytes > size - pos )
      return false;
    uint64 value = 0;
    for ( size_t i = 0; i < bytes; ++i )
      value |= uint64(data[pos++]) << (i * 8);
    *out = value;
    return true;
  }

  bool string(std::string *out)
  {
    // NUL-terminated, length-capped string inside the stream.
    size_t end = pos;
    while ( end < size && data[end] != 0 )
      ++end;
    if ( end >= size )
      return false;
    const size_t length = end - pos;
    if ( length > 4096 )
      return false;
    *out = std::string(reinterpret_cast<const char *>(data + pos), length);
    pos = end + 1;
    return true;
  }

  bool skip(size_t bytes)
  {
    if ( bytes > size - pos )
      return false;
    pos += bytes;
    return true;
  }
};

// Abbreviations
struct abbrev_attr_t
{
  uint32 at = 0;
  uint32 form = 0;
  int64 implicit_const = 0;
};

struct abbrev_decl_t
{
  uint32 tag = 0;
  bool children = false;
  std::vector<abbrev_attr_t> attrs;
};

bool parse_abbrev_table(const dwarf_section_ref_t &section, size_t offset,
                        std::map<uint32, abbrev_decl_t> *table)
{
  if ( section.data == nullptr || offset >= section.size )
    return false;
  dwarf_reader_t r = { section.data, section.size, offset };
  for ( size_t count = 0; count < 1000000; ++count )
  {
    uint64 code;
    if ( !r.uleb(&code) )
      return false;
    if ( code == 0 )
      return true;
    if ( code > UINT32_MAX )
      return false;
    uint64 tag;
    if ( !r.uleb(&tag) || tag > UINT32_MAX || r.pos >= r.size )
      return false;
    abbrev_decl_t decl;
    decl.tag = uint32(tag);
    decl.children = r.data[r.pos++] != 0;
    for ( ;; )
    {
      uint64 at, form;
      if ( !r.uleb(&at) || !r.uleb(&form) )
        return false;
      if ( at == 0 && form == 0 )
        break;
      if ( at > UINT32_MAX || form > UINT32_MAX )
        return false;
      abbrev_attr_t attr;
      attr.at = uint32(at);
      attr.form = uint32(form);
      if ( attr.form == DW_FORM_implicit_const && !r.sleb(&attr.implicit_const) )
        return false;
      decl.attrs.push_back(attr);
    }
    (*table)[uint32(code)] = decl;
  }
  return false;
}

// Attribute values and CU context
struct cu_context_t
{
  uint64 cu_start = 0;
  uint64 cu_end = 0;
  uint64 str_offsets_base = 8; // DWARF5 documented defaults
  uint64 addr_base = 8;
  uchar addr_size = 4;
};

struct attr_value_t
{
  enum kind_t
  {
    kind_none,
    kind_string,
    kind_uint,
    kind_sint,
    kind_ref,
    kind_expr
  };
  kind_t kind = kind_none;
  std::string str;
  uint64 u = 0;
  int64 s = 0;
  uint64 ref = 0;              // CU-relative DIE offset for kind_ref
  const uchar *expr = nullptr; // kind_expr payload
  uint64 expr_size = 0;
};

bool section_string(const dwarf_section_ref_t &section, uint64 offset, std::string *out)
{
  if ( section.data == nullptr || offset >= section.size )
    return false;
  dwarf_reader_t r = { section.data, section.size, size_t(offset) };
  return r.string(out);
}

// Resolves a DWARF5 strx index through .debug_str_offsets + .debug_str. The
// contribution header (unit_length + version) is detected so the function
// works whether the recorded base points at the header or past it.
bool resolve_strx(const dwarf_sections_ref_t &sections, const cu_context_t &cu, uint64 index,
                  std::string *out)
{
  if ( sections.str_offsets == nullptr || sections.str == nullptr
       || sections.str_offsets->data == nullptr || sections.str->data == nullptr )
    return false;
  const dwarf_section_ref_t &table = *sections.str_offsets;
  uint64 entry = cu.str_offsets_base;
  if ( cu.str_offsets_base + 6 <= table.size )
  {
    dwarf_reader_t h = { table.data, table.size, size_t(cu.str_offsets_base) };
    uint64 unit_length;
    if ( h.fixed(&unit_length, 4) && cu.str_offsets_base + 4 + unit_length <= table.size )
    {
      uint64 version;
      if ( h.fixed(&version, 2) && version == 5 )
        entry = cu.str_offsets_base + 6;
    }
  }
  const uint64 slot = entry + index * 4;
  if ( slot + 4 > table.size )
    return false;
  const uint64 str_offset = uint64(table.data[size_t(slot)])
    | uint64(table.data[size_t(slot + 1)]) << 8 | uint64(table.data[size_t(slot + 2)]) << 16
    | uint64(table.data[size_t(slot + 3)]) << 24;
  return section_string(*sections.str, str_offset, out);
}

// Resolves a DWARF5 addrx index through .debug_addr (header auto-detect).
bool resolve_addrx(const dwarf_sections_ref_t &sections, const cu_context_t &cu, uint64 index,
                   uint64 *out)
{
  if ( sections.addr == nullptr || sections.addr->data == nullptr )
    return false;
  const dwarf_section_ref_t &table = *sections.addr;
  uint64 entry = cu.addr_base;
  if ( cu.addr_base + 7 <= table.size )
  {
    dwarf_reader_t h = { table.data, table.size, size_t(cu.addr_base) };
    uint64 unit_length;
    if ( h.fixed(&unit_length, 4) && cu.addr_base + 4 + unit_length <= table.size )
    {
      uint64 version, addr_size;
      if ( h.fixed(&version, 2) && version == 5 && h.fixed(&addr_size, 1)
           && addr_size == cu.addr_size )
        entry = cu.addr_base + 7;
    }
  }
  const uint64 slot = entry + index * cu.addr_size;
  if ( slot + cu.addr_size > table.size )
    return false;
  uint64 value = 0;
  for ( uint64 i = 0; i < cu.addr_size; ++i )
    value |= uint64(table.data[size_t(slot + i)]) << (i * 8);
  *out = value;
  return true;
}

// Decodes one attribute value according to its form. Every branch must
// consume exactly the bytes the form prescribes, otherwise the DIE walk
// desynchronizes; unknown forms return false so the CU is skipped.
bool decode_form(dwarf_reader_t *r, uint32 form, int64 implicit_const,
                 const dwarf_sections_ref_t &sections, const cu_context_t &cu, attr_value_t *out)
{
  attr_value_t value;
  switch ( form )
  {
    case DW_FORM_addr:
      value.kind = attr_value_t::kind_uint;
      if ( !r->fixed(&value.u, cu.addr_size) )
        return false;
      break;
    case DW_FORM_string:
      value.kind = attr_value_t::kind_string;
      if ( !r->string(&value.str) )
        return false;
      break;
    case DW_FORM_strp:
    {
      uint64 offset;
      if ( !r->fixed(&offset, 4) )
        return false;
      value.kind = attr_value_t::kind_string;
      if ( sections.str == nullptr || sections.str->data == nullptr
           || !section_string(*sections.str, offset, &value.str) )
        value.str.clear(); // keep the DIE walkable
      break;
    }
    case DW_FORM_strx:
    case DW_FORM_strx1:
    case DW_FORM_strx2:
    case DW_FORM_strx3:
    case DW_FORM_strx4:
    {
      uint64 index;
      if ( form == DW_FORM_strx )
      {
        if ( !r->uleb(&index) )
          return false;
      }
      else if ( !r->fixed(&index, size_t(form - DW_FORM_strx1) + 1) )
        return false;
      value.kind = attr_value_t::kind_string;
      if ( !resolve_strx(sections, cu, index, &value.str) )
        value.str.clear();
      break;
    }
    case DW_FORM_addrx:
    case DW_FORM_addrx1:
    case DW_FORM_addrx2:
    case DW_FORM_addrx3:
    case DW_FORM_addrx4:
    {
      uint64 index;
      if ( form == DW_FORM_addrx )
      {
        if ( !r->uleb(&index) )
          return false;
      }
      else if ( !r->fixed(&index, size_t(form - DW_FORM_addrx1) + 1) )
        return false;
      value.kind = attr_value_t::kind_uint;
      if ( !resolve_addrx(sections, cu, index, &value.u) )
        value.u = UINT64_MAX; // unknown address, still walkable
      break;
    }
    case DW_FORM_ref1:
    case DW_FORM_ref2:
    case DW_FORM_ref4:
    case DW_FORM_ref8:
    case DW_FORM_ref_udata:
      value.kind = attr_value_t::kind_ref;
      if ( form == DW_FORM_ref_udata )
      {
        if ( !r->uleb(&value.ref) )
          return false;
      }
      else if ( !r->fixed(&value.ref,
                          form == DW_FORM_ref1     ? 1
                            : form == DW_FORM_ref2 ? 2
                            : form == DW_FORM_ref4 ? 4
                                                   : 8) )
        return false;
      break;
    case DW_FORM_data1:
    case DW_FORM_data2:
    case DW_FORM_data4:
    case DW_FORM_data8:
    case DW_FORM_data16:
      value.kind = attr_value_t::kind_uint;
      if ( !r->fixed(&value.u,
                     size_t(form == DW_FORM_data1      ? 1
                              : form == DW_FORM_data2  ? 2
                              : form == DW_FORM_data4  ? 4
                              : form == DW_FORM_data16 ? 16
                                                       : 8)) )
        return false;
      break;
    case DW_FORM_sdata:
      value.kind = attr_value_t::kind_sint;
      if ( !r->sleb(&value.s) )
        return false;
      break;
    case DW_FORM_udata:
      value.kind = attr_value_t::kind_uint;
      if ( !r->uleb(&value.u) )
        return false;
      break;
    case DW_FORM_flag:
      value.kind = attr_value_t::kind_uint;
      if ( !r->fixed(&value.u, 1) )
        return false;
      break;
    case DW_FORM_flag_present:
      value.kind = attr_value_t::kind_uint;
      value.u = 1;
      break;
    case DW_FORM_sec_offset:
      value.kind = attr_value_t::kind_uint;
      if ( !r->fixed(&value.u, 4) )
        return false;
      break;
    case DW_FORM_exprloc:
    {
      uint64 length;
      if ( !r->uleb(&length) || length > r->size - r->pos )
        return false;
      value.kind = attr_value_t::kind_expr;
      value.expr = r->data + r->pos;
      value.expr_size = length;
      if ( !r->skip(size_t(length)) )
        return false;
      break;
    }
    case DW_FORM_block:
    case DW_FORM_block1:
    case DW_FORM_block2:
    case DW_FORM_block4:
    {
      uint64 length;
      if ( form == DW_FORM_block )
      {
        if ( !r->uleb(&length) )
          return false;
      }
      else if ( !r->fixed(&length, form == DW_FORM_block1 ? 1 : form == DW_FORM_block2 ? 2 : 4) )
        return false;
      if ( length > r->size - r->pos )
        return false;
      value.kind = attr_value_t::kind_none; // opaque block, skipped
      if ( !r->skip(size_t(length)) )
        return false;
      break;
    }
    case DW_FORM_implicit_const:
      value.kind = attr_value_t::kind_sint;
      value.s = implicit_const;
      break;
    case DW_FORM_rnglistx:
    case DW_FORM_loclistx:
      value.kind = attr_value_t::kind_uint;
      if ( !r->uleb(&value.u) )
        return false;
      break;
    case DW_FORM_ref_sig8:
      value.kind = attr_value_t::kind_uint;
      if ( !r->fixed(&value.u, 8) )
        return false;
      break;
    case DW_FORM_ref_sup4:
    case DW_FORM_strp_sup:
    case DW_FORM_line_strp:
      value.kind = attr_value_t::kind_uint;
      if ( !r->fixed(&value.u, 4) )
        return false;
      break;
    case DW_FORM_ref_sup8:
      value.kind = attr_value_t::kind_uint;
      if ( !r->fixed(&value.u, 8) )
        return false;
      break;
    case DW_FORM_indirect:
    {
      uint64 inner;
      if ( !r->uleb(&inner) || inner > UINT32_MAX )
        return false;
      return decode_form(r, uint32(inner), implicit_const, sections, cu, out);
    }
    default: return false;
  }
  *out = value;
  return true;
}

int64 wasm_location_local(const attr_value_t &location)
{
  if ( location.kind != attr_value_t::kind_expr || location.expr == nullptr
       || location.expr_size < 2 )
    return -1;
  if ( location.expr[0] != DW_OP_WASM_location )
    return -1;
  dwarf_reader_t r = { location.expr, size_t(location.expr_size), 1 };
  uint64 kind;
  if ( !r.uleb(&kind) || kind != 0 )
    return -1;
  uint64 index;
  if ( !r.uleb(&index) || index > UINT32_MAX )
    return -1;
  return int64(index);
}

bool location_static_address(const attr_value_t &location, uint64 *address)
{
  if ( location.kind != attr_value_t::kind_expr || location.expr == nullptr
       || location.expr_size < 5 )
    return false;
  if ( location.expr[0] != DW_OP_addr )
    return false;
  uint64 value = 0;
  for ( uint64 i = 0; i + 1 < location.expr_size && i < 4; ++i )
    value |= uint64(location.expr[size_t(i + 1)]) << (i * 8);
  *address = value;
  return true;
}

const attr_value_t *find_attr(const std::vector<uint32> &ats,
                              const std::vector<attr_value_t> &values, uint32 wanted)
{
  for ( size_t i = 0; i < ats.size() && i < values.size(); ++i )
    if ( ats[i] == wanted )
      return &values[i];
  return nullptr;
}

bool wanted_attribute(uint32 at)
{
  switch ( at )
  {
    case DW_AT_name:
    case DW_AT_low_pc:
    case DW_AT_location:
    case DW_AT_abstract_origin:
    case DW_AT_specification:
    case DW_AT_declaration:
    case DW_AT_str_offsets_base:
    case DW_AT_addr_base: return true;
    default: return false; // decoded but discarded (bounds memory on huge CUs)
  }
}

// DIE stack frame: remembers which subprogram (if any) collects children.
struct die_frame_t
{
  uint32 tag = 0;
  int64 subprogram = -1; // index into results->subprograms
};

// Walks one compile unit, collecting subprograms and module-level variables.
void parse_compile_unit(const dwarf_section_ref_t &info, const dwarf_section_ref_t &abbrev,
                        const dwarf_sections_ref_t &sections, uint64 cu_start, uint64 unit_length,
                        uint16 version, dwarf_results_t *results)
{
  dwarf_reader_t r = { info.data, info.size, size_t(cu_start + 6) };
  cu_context_t cu;
  cu.cu_start = cu_start;
  cu.cu_end = cu_start + 4 + unit_length;
  uint64 abbrev_offset = 0;
  if ( version == 5 )
  {
    uint64 unit_type, addr_size;
    if ( !r.fixed(&unit_type, 1) || !r.fixed(&addr_size, 1) || !r.fixed(&abbrev_offset, 4) )
      return;
    if ( (unit_type != DW_UT_compile && unit_type != DW_UT_partial
          && unit_type != DW_UT_split_compile)
         || addr_size == 0 || addr_size > 8 )
    {
      ++results->skipped_units;
      return;
    }
    cu.addr_size = uchar(addr_size);
    if ( unit_type == DW_UT_split_compile && !r.skip(8) ) // dwo_id
      return;
    if ( unit_type == DW_UT_skeleton && !r.skip(8) )
      return;
  }
  else
  {
    uint64 addr_size;
    if ( !r.fixed(&abbrev_offset, 4) || !r.fixed(&addr_size, 1) || addr_size == 0 || addr_size > 8 )
      return;
    cu.addr_size = uchar(addr_size);
  }
  if ( r.pos > cu.cu_end )
    return;

  std::map<uint32, abbrev_decl_t> table;
  if ( !parse_abbrev_table(abbrev, size_t(abbrev_offset), &table) )
  {
    ++results->skipped_units;
    return;
  }

  std::vector<die_frame_t> stack;
  size_t die_count = 0;
  // CU-relative DIE offset -> subprogram index (for abstract-origin lookup)
  std::map<uint64, size_t> subprogram_by_die;
  // CU-relative DIE offset -> parameter/variable name (for per-entry origins)
  std::map<uint64, std::string> entry_name_by_die;
  const size_t first_subprogram = results->subprograms.size();

  while ( r.pos < cu.cu_end )
  {
    const uint64 die_abs = r.pos;
    uint64 code;
    if ( !r.uleb(&code) )
      break;
    if ( code == 0 )
    {
      // NULL entry closes the innermost children list.
      if ( stack.empty() )
        break;
      stack.pop_back();
      continue;
    }
    if ( ++die_count > dwarf_max_dies )
      break;
    const auto it = table.find(uint32(code));
    if ( it == table.end() )
      break; // desynchronized abbreviations: abandon this CU
    const abbrev_decl_t &decl = it->second;

    std::vector<uint32> ats;
    std::vector<attr_value_t> values;
    bool walkable = true;
    for ( size_t a = 0; a < decl.attrs.size(); ++a )
    {
      attr_value_t value;
      if ( !decode_form(&r, decl.attrs[a].form, decl.attrs[a].implicit_const, sections, cu,
                        &value) )
      {
        walkable = false;
        break;
      }
      if ( wanted_attribute(decl.attrs[a].at) )
      {
        ats.push_back(decl.attrs[a].at);
        values.push_back(value);
      }
    }
    if ( !walkable )
      break;
    const uint64 cu_relative = die_abs - cu.cu_start;

    if ( stack.empty() )
    {
      // Compilation-unit root DIE: pick up the DWARF5 bases.
      const attr_value_t *str_base = find_attr(ats, values, DW_AT_str_offsets_base);
      if ( str_base != nullptr )
        cu.str_offsets_base = str_base->u;
      const attr_value_t *addr_base = find_attr(ats, values, DW_AT_addr_base);
      if ( addr_base != nullptr )
        cu.addr_base = addr_base->u;
      if ( decl.children )
        stack.push_back({ decl.tag, -1 });
      continue;
    }

    const die_frame_t &parent = stack.back();
    if ( decl.tag == DW_TAG_subprogram )
    {
      dwarf_subprogram_t sp;
      sp.die = cu_relative;
      const attr_value_t *name = find_attr(ats, values, DW_AT_name);
      if ( name != nullptr && name->kind == attr_value_t::kind_string && !name->str.empty() )
      {
        sp.name = name->str;
        sp.has_name = true;
      }
      const attr_value_t *low_pc = find_attr(ats, values, DW_AT_low_pc);
      if ( low_pc != nullptr && low_pc->kind == attr_value_t::kind_uint && low_pc->u != UINT64_MAX )
      {
        sp.has_low_pc = true;
        sp.low_pc = low_pc->u;
      }
      const attr_value_t *declaration = find_attr(ats, values, DW_AT_declaration);
      sp.declaration_only = declaration != nullptr && declaration->kind == attr_value_t::kind_uint
        && declaration->u != 0;
      const attr_value_t *origin = find_attr(ats, values, DW_AT_abstract_origin);
      if ( origin == nullptr )
        origin = find_attr(ats, values, DW_AT_specification);
      if ( origin != nullptr && origin->kind == attr_value_t::kind_ref )
        sp.origin = origin->ref;
      subprogram_by_die[cu_relative] = results->subprograms.size();
      results->subprograms.push_back(sp);
      if ( decl.children )
        stack.push_back({ decl.tag, int64(results->subprograms.size() - 1) });
      continue;
    }
    if ( decl.tag == DW_TAG_formal_parameter || decl.tag == DW_TAG_variable )
    {
      const attr_value_t *declaration = find_attr(ats, values, DW_AT_declaration);
      const bool declaration_only = declaration != nullptr
        && declaration->kind == attr_value_t::kind_uint && declaration->u != 0;
      if ( !declaration_only )
      {
        dwarf_local_entry_t entry;
        entry.is_parameter = decl.tag == DW_TAG_formal_parameter;
        const attr_value_t *name = find_attr(ats, values, DW_AT_name);
        if ( name != nullptr && name->kind == attr_value_t::kind_string && !name->str.empty() )
        {
          entry.name = name->str;
          entry.has_name = true;
        }
        const attr_value_t *location = find_attr(ats, values, DW_AT_location);
        if ( location != nullptr )
          entry.wasm_local = wasm_location_local(*location);
        const attr_value_t *origin = find_attr(ats, values, DW_AT_abstract_origin);
        if ( origin == nullptr )
          origin = find_attr(ats, values, DW_AT_specification);
        if ( origin != nullptr && origin->kind == attr_value_t::kind_ref )
          entry.origin = origin->ref;
        if ( entry.has_name )
          entry_name_by_die[cu_relative] = entry.name;
        if ( parent.subprogram >= 0 )
          results->subprograms[size_t(parent.subprogram)].entries.push_back(entry);
        else if ( stack.size() == 1 && parent.tag == DW_TAG_compile_unit
                  && decl.tag == DW_TAG_variable )
        {
          // Module-level variable: map its static address to a data segment.
          uint64 address = 0;
          if ( entry.has_name && location != nullptr
               && location_static_address(*location, &address) )
          {
            dwarf_data_variable_t variable;
            variable.name = entry.name;
            variable.address = address;
            results->data_variables.push_back(variable);
          }
        }
      }
      // Parameters/variables have no children in practice; if they do, a new
      // frame keeps the NULL balancing correct without attaching anything.
      if ( decl.children )
        stack.push_back({ decl.tag, -1 });
      continue;
    }
    // Tags (structure/member/base/pointer/...) decode through the same
    // form walker; stores nothing for them. Inlined subroutines and
    // lexical blocks get neutral frames so their locals are skipped cleanly.
    if ( decl.children )
    {
      if ( stack.size() < dwarf_max_stack )
        stack.push_back({ decl.tag, -1 });
      else
        break;
    }
  }

  // Resolve abstract origins inside this CU (clang emits abstract DIEs with
  // the names first, concrete instances reference them).
  for ( size_t i = first_subprogram; i < results->subprograms.size(); ++i )
  {
    dwarf_subprogram_t &sp = results->subprograms[i];
    if ( sp.has_name && !sp.entries.empty() )
      continue;
    uint64 hop = sp.origin;
    for ( unsigned depth = 0; depth < 4 && hop != UINT64_MAX; ++depth )
    {
      const auto source = subprogram_by_die.find(hop);
      if ( source == subprogram_by_die.end() )
        break;
      const dwarf_subprogram_t &origin_sp = results->subprograms[source->second];
      if ( !sp.has_name && origin_sp.has_name )
      {
        sp.name = origin_sp.name;
        sp.has_name = true;
      }
      if ( sp.entries.empty() && !origin_sp.entries.empty() )
        sp.entries = origin_sp.entries;
      hop = origin_sp.origin;
    }
    for ( size_t e = 0; e < sp.entries.size(); ++e )
    {
      dwarf_local_entry_t &entry = sp.entries[e];
      if ( entry.has_name || entry.origin == UINT64_MAX )
        continue;
      const auto named = entry_name_by_die.find(entry.origin);
      if ( named != entry_name_by_die.end() )
      {
        entry.name = named->second;
        entry.has_name = true;
      }
    }
  }
}
} // anonymous namespace

bool section_uleb_size(const uchar *data, size_t size, size_t *pos, uint64 *value)
{
  uint64 result = 0;
  unsigned shift = 0;
  for ( unsigned i = 0; i < 5; ++i )
  {
    if ( *pos >= size )
      return false;
    const uchar b = data[(*pos)++];
    result |= uint64(b & 0x7F) << shift;
    if ( (b & 0x80) == 0 )
    {
      *value = result;
      return true;
    }
    shift += 7;
  }
  return false;
}

bool parse_dwarf_units(const dwarf_section_ref_t &info, const dwarf_section_ref_t &abbrev,
                       const dwarf_sections_ref_t &sections, dwarf_results_t *results,
                       bool *saw_dwarf64)
{
  dwarf_reader_t r = { info.data, info.size, 0 };
  size_t units = 0;
  bool reported_dwarf64 = false;
  while ( r.pos + 4 <= r.size )
  {
    const uint64 cu_start = r.pos;
    uint64 unit_length;
    if ( !r.fixed(&unit_length, 4) )
      break;
    if ( unit_length == 0xFFFFFFFF )
    {
      if ( !reported_dwarf64 )
      {
        if ( saw_dwarf64 != nullptr )
          *saw_dwarf64 = true;
        reported_dwarf64 = true;
      }
      break;
    }
    if ( unit_length == 0 || r.pos + unit_length > r.size )
      break;
    uint64 version;
    if ( !r.fixed(&version, 2) )
      break;
    if ( version >= 4 && version <= 5 )
    {
      parse_compile_unit(info, abbrev, sections, cu_start, unit_length, uint16(version), results);
    }
    else
      ++results->skipped_units;
    r.pos = cu_start + 4 + unit_length;
    if ( ++units > dwarf_max_units )
      break;
  }
  return true;
}

} // namespace wasmhex::loader
