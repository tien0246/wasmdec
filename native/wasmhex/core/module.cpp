// Pure-C++ core of the wasmhex loader front end: only the
// standard library is used here; the loader/ side converts to IDA SDK types.
#include "types.hpp"
#include "binary/instruction_stream.hpp"
#include "validation/function_validator.hpp"
#ifdef WASMHEX_REPO_DIR
#include "../tests/doctest.h"
#endif

#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <limits>

namespace wasmhex::loader
{

namespace
{

// printf-style formatting into std::string (replaces qstring::sprnt); the
// declaration is shared through core/types.hpp.
std::string format_impl(const char *fmt, va_list args)
{
  char buffer[512];
  const int written = std::vsnprintf(buffer, sizeof(buffer), fmt, args);
  if ( written <= 0 )
    return std::string();
  if ( size_t(written) < sizeof(buffer) )
    return std::string(buffer, size_t(written));
  std::string large(size_t(written), '\0');
  std::vsnprintf(&large[0], large.size() + 1, fmt, args);
  return large;
}

} // anonymous namespace

std::string format(const char *fmt, ...)
{
  va_list args;
  va_start(args, fmt);
  std::string result = format_impl(fmt, args);
  va_end(args);
  return result;
}

// The single rejection sink — code first, string as display payload.
void module_t::reject(error_code code, const char *reason)
{
  if ( code == error_code::none || diagnostic.code != error_code::none )
    return; // no code, or an earlier rejection already won
  // Copy into owned storage: some callers pass a temporary std::string's
  // c_str() and the pointer must outlive the full expression (the fuzz
  // driver caught the use-after-free in validate_module).
  failure_reason_storage = reason != nullptr ? reason : "";
  failure_reason = failure_reason_storage.c_str();
  diagnostic.code = code;
  diagnostic.cls = class_for_code(code);
  diagnostic.detail = failure_reason_storage;
}

std::string imported_global_helper_name(uint32 index, const std::string &module,
                                        const std::string &field)
{
  std::string name;
  name = format("wasm_global_import_%u_", index);
  for ( size_t i = 0; i < module.length() && i < 48; ++i )
  {
    const char c = module[i];
    name.append(
      1,
      (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' ? c
                                                                                             : '_');
  }
  name.append("_");
  for ( size_t i = 0; i < field.length() && i < 48; ++i )
  {
    const char c = field[i];
    name.append(
      1,
      (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' ? c
                                                                                             : '_');
  }
  return name;
}

bool is_abstract_reference_handle(uchar type, int64 heap_type)
{
  return (type == 0x70 && (heap_type == no_heap_type || heap_type == -0x10))
    || (type == 0x6F && (heap_type == no_heap_type || heap_type == -0x11));
}

size_t wasm_results_byte_size(const std::vector<uchar> &results)
{
  if ( results.size() < 2 )
    return 0;
  size_t size = 0;
  for ( uchar type : results )
    size += type == 0x7E || type == 0x7C ? 8 : 4;
  return size;
}

bool module_t::read_value_type(reader_t *r, uchar *type, int64 *heap_type, uchar *ref_form)
{
  // Thin adapter over the shared decoder in wasmhex_registers.hpp so the
  // loader and the processor agree on value-type / heap-type decoding.
  const size_t start = r->pos;
  wasmhex_vt::value_type_t decoded;
  const wasmhex_vt::value_type_status_t status
    = wasmhex_vt::decode_value_type(r->data, r->size, &r->pos, &decoded);
  if ( status != wasmhex_vt::vt_ok )
  {
    r->pos = start;
    if ( status == wasmhex_vt::vt_unsupported_heap )
      reject(
        error_code::policy_gc_types,
        "Unsupported WebAssembly reference heap type; wasmhex supports function, extern and eq references");
    return false;
  }
  *type = decoded.canonical_type;
  if ( heap_type != nullptr )
    *heap_type = decoded.heap_type;
  if ( ref_form != nullptr )
    *ref_form = decoded.ref_kind;
  // Historical behavior: the eqref flag only triggers for decoded reference
  // forms, while the externref flag tracks the canonical byte everywhere.
  has_generic_eqref |= decoded.is_reference() && *type == 0x71;
  has_externref |= *type == 0x6F;
  return true;
}

bool module_t::validate_heap_type_index(int64 heap_type)
{
  if ( heap_type < 0 || uint64(heap_type) < types.size() )
    return true;
  reject(error_code::bad_type_section, "Invalid WebAssembly function reference type index");
  return false;
}

bool skip_sleb(reader_t *r, unsigned bits)
{
  const unsigned max_bytes = (bits + 6) / 7;
  unsigned shift = 0;
  for ( unsigned i = 0; i < max_bytes; ++i )
  {
    uchar b;
    if ( !r->byte(&b) )
      return false;
    if ( (b & 0x80) == 0 )
    {
      const unsigned remaining = bits - shift;
      if ( remaining < 7 )
      {
        const uchar low_mask = uchar((1u << remaining) - 1);
        const uchar high_mask = uchar(0x7F ^ low_mask);
        const uchar payload = b & 0x7F;
        const bool negative = (b & 0x40) != 0;
        if ( negative ? (payload & high_mask) != high_mask : (payload & high_mask) != 0 )
          return false;
      }
      return true;
    }
    shift += 7;
  }
  return false;
}

void append_uleb(bytevec *out, uint64 value)
{
  do
  {
    uchar byte = uchar(value & 0x7F);
    value >>= 7;
    if ( value != 0 )
      byte |= 0x80;
    out->push_back(byte);
  } while ( value != 0 );
}

size_t uleb_size(uint64 value)
{
  size_t size = 1;
  while ( value >= 0x80 )
  {
    value >>= 7;
    ++size;
  }
  return size;
}

bool module_t::skip_supported_instruction(reader_t *r, uchar opcode, bool memory64,
                                          size_t memory_count)
{
  // Delegates to the single instruction-stream walker (`insn_cursor_t`
  // + `next_insn`) over the instruction starting at `r->pos - 1`, keeping
  // the additional module-level checks for 0x1C (value-type + heap bounds)
  // and 0x3F/0x40 (single memory at index 0).
  (void)memory64;
  if ( r == nullptr || r->pos == 0 || r->data[r->pos - 1] != opcode )
    return false;
  if ( opcode == 0x1C )
  {
    uint64 count;
    uchar type;
    int64 heap_type;
    uchar ref_form;
    return r->uleb(&count) && count == 1 && read_value_type(r, &type, &heap_type, &ref_form)
      && validate_heap_type_index(heap_type)
      && (type == 0x7F || type == 0x7E || type == 0x7D || type == 0x7C || type == 0x70
          || type == 0x6F || type == 0x71 || type == 0x72);
  }
  binary::insn_cursor_t cursor{ r->data, r->size, r->pos - 1, nullptr };
  binary::decoded_insn_t decoded;
  if ( !binary::next_insn(cursor, &decoded) )
    return false;
  if ( (opcode == 0x3F || opcode == 0x40) && (memory_count != 1 || decoded.imm[0] != 0) )
    return false;
  r->pos = cursor.pos;
  return true;
}

uint8 section_order(uchar id)
{
  switch ( id )
  {
    case 1: return 1;   // type
    case 2: return 2;   // import
    case 3: return 3;   // function
    case 4: return 4;   // table
    case 5: return 5;   // memory
    case 13: return 6;  // tag
    case 6: return 7;   // global
    case 7: return 8;   // export
    case 8: return 9;   // start
    case 9: return 10;  // element
    case 12: return 11; // data count
    case 10: return 12; // code
    case 11: return 13; // data
    default: return 0;
  }
}

bool module_t::function_type_index(uint64 function_index, uint32 *type_index)
{
  if ( function_index < imported_func_types.size() )
    *type_index = imported_func_types[size_t(function_index)];
  else
  {
    function_index -= imported_func_types.size();
    if ( function_index >= defined_func_types.size() )
    {
      reject(error_code::bad_element,
             "Element segment references an invalid WebAssembly function index");
      return false;
    }
    *type_index = defined_func_types[size_t(function_index)];
  }
  if ( *type_index >= types.size() )
  {
    reject(error_code::bad_element,
           "Element segment function has an invalid WebAssembly type index");
    return false;
  }
  return true;
}

const table_slot_t *module_t::find_table_slot(uint32 table_index, uint32 slot_index) const
{
  size_t low = 0;
  size_t high = table_slots.size();
  while ( low < high )
  {
    const size_t middle = low + (high - low) / 2;
    const table_slot_t &slot = table_slots[middle];
    if ( slot.table_index < table_index
         || (slot.table_index == table_index && slot.slot_index < slot_index) )
      low = middle + 1;
    else
      high = middle;
  }
  if ( low == table_slots.size() || table_slots[low].table_index != table_index
       || table_slots[low].slot_index != slot_index )
    return nullptr;
  return &table_slots[low];
}

bool module_t::resolve_c_stack_global()
{
  if ( c_stack_global_resolved )
    return true;
  c_stack_global_resolved = true;
  c_stack_global_index = UINT32_MAX;
  const std::string target_field
    = c_stack_global_override.empty() ? std::string("__stack_pointer") : c_stack_global_override;
  uint32 fallback_match = UINT32_MAX;
  for ( size_t i = 0; i < globals.size(); ++i )
  {
    const global_t &global = globals[i];
    if ( !global.imported || global.field_name != target_field )
      continue;
    if ( global.type != 0x7F || global.mutability != 1 )
      continue;
    if ( global.module_name == "env" )
    {
      c_stack_global_index = uint32(i);
      return true;
    }
    if ( fallback_match == UINT32_MAX )
      fallback_match = uint32(i);
  }
  if ( fallback_match != UINT32_MAX )
  {
    // A __stack_pointer import from a
    // NON-env module is only accepted when a function prologue proves the
    // global is used as the shadow stack pointer (`global.get idx;
    // local.set k` at a body head). Without the proof the module is
    // rejected with a reason — guessing the wrong global corrupts every
    // C-frame recovery downstream.
    for ( const body_t &body : bodies )
    {
      if ( body.offset >= code.size() )
        continue;
      const uchar *p = code.data() + body.offset;
      const size_t avail = std::min(size_t(body.size), code.size() - size_t(body.offset));
      size_t at = 0;
      if ( avail <= at || p[at] != 0x23 ) // global.get
        continue;
      ++at;
      uint64 index = 0;
      unsigned shift = 0;
      bool decoded = false;
      while ( at < avail )
      {
        const uchar byte = p[at++];
        index |= uint64(byte & 0x7F) << shift;
        if ( (byte & 0x80) == 0 )
        {
          decoded = true;
          break;
        }
        shift += 7;
      }
      if ( !decoded || index != fallback_match || at >= avail || p[at] != 0x21 ) // local.set
        continue;
      c_stack_global_index = fallback_match;
      return true;
    }
    reject(error_code::policy_other,
           ("Imported mutable i32 '" + target_field
            + "' is not the 'env' stack pointer and no function prologue "
              "proves it is the shadow stack pointer (global.get; local.set "
              "at a body head)")
             .c_str());
    return false;
  }
  if ( !c_stack_global_override.empty() )
  {
    reject(error_code::policy_other,
           "Configured WASMHEX_C_STACK_GLOBAL was not found as a mutable i32 imported global");
    return false;
  }
  return true;
}

const char *c_type(uchar wasm_type)
{
  switch ( wasm_type )
  {
    case 0x7F: return "int";
    case 0x7E: return "__int64";
    case 0x7D: return "float";
    case 0x7C: return "double";
    case 0x70: // funcref handle
    case 0x6F: // externref handle
    case 0x71: // eqref-family identity handle
    case 0x72: // tagged i31 handle
      return "unsigned int";
    default: return nullptr;
  }
}

bool valid_identifier(const std::string &name)
{
  if ( name.empty() )
    return false;
  for ( size_t i = 0; i < name.length(); ++i )
  {
    const char c = name[i];
    if ( !((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_' || c == '$' || c == '.'
           || c == '<' || c == '>' || c == ':' || c == '-' || (i != 0 && c >= '0' && c <= '9')) )
      return false;
  }
  return true;
}

std::string sanitize_identifier(const std::string &raw)
{
  std::string name;
  const size_t length = raw.length() < 48 ? raw.length() : 48;
  for ( size_t i = 0; i < length; ++i )
  {
    const char c = raw[i];
    if ( i == 0 && c >= '0' && c <= '9' )
      name.append(1, '_');
    name.append(
      1,
      (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' ? c
                                                                                             : '_');
  }
  return name;
}

std::string go_method_display_name(const std::string &raw)
{
  const size_t dot = raw.rfind('.');
  if ( dot == std::string::npos || dot == 0 )
    return raw;
  const std::string owner = raw.substr(0, dot);
  const size_t seg_dot = owner.rfind('.');
  const std::string segment = owner.substr(seg_dot == std::string::npos ? 0 : seg_dot + 1);
  // Mirror of the anchor-4 mangling test (toolchains/go/types.cpp):
  // a `__T_` tail segment is `(*T)`. Package-less symbols never
  // transform (the Go symbol table always qualifies with a package).
  if ( seg_dot != std::string::npos && segment.size() > 3 && segment.compare(0, 2, "__") == 0
       && segment.back() == '_' )
  {
    std::string out = owner.substr(0, seg_dot);
    out += ".(*";
    out += segment.substr(2, segment.size() - 3);
    out += ").";
    out += raw.substr(dot + 1);
    return out;
  }
  return raw;
}

std::string imported_global_name(uint32 index, const std::string &module, const std::string &field,
                                 std::unordered_set<std::string> *used_names)
{
  std::string name = sanitize_identifier(field);
  if ( !valid_identifier(name) )
    name = format("wasm_global_%u", index);
  if ( used_names != nullptr && !used_names->insert(std::string(name.c_str())).second )
  {
    const std::string module_part = sanitize_identifier(module);
    if ( valid_identifier(module_part) )
    {
      std::string prefixed = module_part;
      prefixed.append("_");
      prefixed.append(name);
      if ( used_names->insert(std::string(prefixed.c_str())).second )
        return prefixed;
      name = prefixed;
    }
    std::string suffixed;
    suffixed = format("%s_%u", name.c_str(), index);
    used_names->insert(std::string(suffixed.c_str()));
    return suffixed;
  }
  return name;
}

std::string function_name(uint32 index, const std::vector<export_t> &exports,
                          const std::vector<export_t> &names,
                          std::unordered_set<std::string> *used_names,
                          const std::string *pclntab_name)
{
  std::string name;
  bool found = false;
  // Name priority: the name section (DWARF fills the
  // same fields) carries the compile-time symbol — the most specific name —
  // then the pclntab (Go source names), and the EXPORT name is the LAST
  // source, used only when nothing else named the function: an export is
  // a public-interface alias, not the function's identity. The previous
  // order put exports first, hiding `wasm_export_run` behind `run` on
  // gowasm-balls func[1430].
  for ( size_t i = 0; i < names.size(); ++i )
  {
    if ( names[i].index == index && valid_identifier(names[i].name) )
    {
      name = names[i].name;
      found = true;
      break;
    }
  }
  if ( !found && pclntab_name != nullptr && !pclntab_name->empty() )
  {
    // Go source names keep their dotted form when they are valid IDA
    // identifiers (runtime.memeq, main.main); the mangled-looking ones
    // ((*mcache).foo, type:.eq.*) fall back to the standard sanitizer.
    name = valid_identifier(*pclntab_name) ? *pclntab_name : sanitize_identifier(*pclntab_name);
    found = true;
  }
  if ( !found )
  {
    for ( size_t i = 0; i < exports.size(); ++i )
    {
      if ( exports[i].index == index && valid_identifier(exports[i].name) )
      {
        name = exports[i].name;
        found = true;
        break;
      }
    }
  }
  if ( !found )
  {
    name = format("wasm_func_%u", index);
  }

  if ( used_names != nullptr )
  {
    if ( !used_names->insert(std::string(name.c_str())).second )
    {
      std::string suffixed;
      suffixed = format("%s_%u", name.c_str(), index);
      used_names->insert(std::string(suffixed.c_str()));
      name = suffixed;
    }
  }
  return name;
}

std::string imported_function_name(uint32 index, const std::string &module,
                                   const std::string &field, const module_t &mod,
                                   std::unordered_set<std::string> *used_names)
{
  std::string name;
  if ( valid_identifier(field) )
  {
    // Check if field conflicts with other imported functions with different module
    bool conflict = false;
    for ( size_t j = 0; j < mod.function_imports.size(); ++j )
    {
      if ( j != index && mod.function_imports[j].name == field
           && mod.function_imports[j].module != module )
      {
        conflict = true;
        break;
      }
    }
    // Check if field conflicts with defined functions
    if ( !conflict )
    {
      for ( size_t j = 0; j < mod.function_exports.size(); ++j )
      {
        if ( mod.function_exports[j].index >= mod.function_imports.size()
             && mod.function_exports[j].name == field )
        {
          conflict = true;
          break;
        }
      }
    }
    if ( !conflict )
    {
      for ( size_t j = 0; j < mod.function_names.size(); ++j )
      {
        if ( mod.function_names[j].index >= mod.function_imports.size()
             && mod.function_names[j].name == field )
        {
          conflict = true;
          break;
        }
      }
    }

    if ( conflict )
    {
      std::string module_part = sanitize_identifier(module);
      if ( valid_identifier(module_part) )
      {
        name = module_part;
        name.append("_");
        name.append(field);
      }
      else
      {
        name = field;
      }
    }
    else
    {
      name = field;
    }
  }
  else
  {
    name = format("wasm_import_%u_", index);
    for ( size_t i = 0; i < module.length() && i < 48; ++i )
    {
      const char c = module[i];
      name.append(1,
                  (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')
                      || c == '_'
                    ? c
                    : '_');
    }
    name.append("_");
    const size_t length = field.length() < 48 ? field.length() : 48;
    for ( size_t i = 0; i < length; ++i )
    {
      const char c = field[i];
      name.append(1,
                  (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')
                      || c == '_'
                    ? c
                    : '_');
    }
  }

  if ( used_names != nullptr )
  {
    if ( !used_names->insert(std::string(name.c_str())).second )
    {
      std::string suffixed;
      suffixed = format("%s_%u", name.c_str(), index);
      used_names->insert(std::string(suffixed.c_str()));
      name = suffixed;
    }
  }
  return name;
}

void resolve_got_globals(module_t &module, std::unordered_map<uint32, std::string> *got_names)
{
  if ( module.start_function_body.data == nullptr || module.start_function_body.size == 0 )
    return;

  uint32 memory_base_index = UINT32_MAX;
  uint32 table_base_index = UINT32_MAX;
  for ( size_t i = 0; i < module.globals.size(); ++i )
  {
    if ( module.globals[i].imported )
    {
      if ( module.globals[i].field_name == "__memory_base" )
        memory_base_index = uint32(i);
      else if ( module.globals[i].field_name == "__table_base" )
        table_base_index = uint32(i);
    }
  }

  std::unordered_map<uint64, std::string> mem_offset_to_name;
  for ( const export_t &exp : module.global_exports )
  {
    if ( exp.index < module.globals.size() && valid_identifier(exp.name)
         && mem_offset_to_name.find(module.globals[exp.index].initial_bits)
           == mem_offset_to_name.end() )
      mem_offset_to_name[module.globals[exp.index].initial_bits] = exp.name;
  }

  std::unordered_map<uint32, uint32> elem_slot_to_func;
  for ( const auto &elem : module.elem_entries )
  {
    elem_slot_to_func[elem.slot_index] = elem.function_index;
  }

  std::unordered_map<uint32, std::string> func_names;
  for ( const export_t &fn : module.function_names )
    if ( valid_identifier(fn.name) && func_names.find(fn.index) == func_names.end() )
      func_names[fn.index] = fn.name;
  for ( size_t i = 0; i < module.pclntab_function_names.size(); ++i )
  {
    const uint32 idx = uint32(module.imported_func_types.size() + i);
    const std::string &pcln = module.pclntab_function_names[i];
    if ( !pcln.empty() && func_names.find(idx) == func_names.end() )
      func_names[idx] = valid_identifier(pcln) ? pcln : sanitize_identifier(pcln);
  }
  for ( const export_t &exp : module.function_exports )
    if ( valid_identifier(exp.name) && func_names.find(exp.index) == func_names.end() )
      func_names[exp.index] = exp.name;

  reader_t r = module.start_function_body;
  uint64 local_groups;
  if ( !r.vector_count(&local_groups) )
    return;
  for ( uint64 j = 0; j < local_groups; ++j )
  {
    uint64 count;
    uchar type;
    int64 heap_type;
    uchar ref_form;
    if ( !r.uleb(&count) || !module.read_value_type(&r, &type, &heap_type, &ref_form) )
      return;
  }

  struct got_val_t
  {
    uint32 base_global = UINT32_MAX;
    int64 offset = 0;
  };
  std::vector<got_val_t> stack;
  std::unordered_map<uint32, got_val_t> locals;
  uchar op;
  while ( r.pos < r.size && r.byte(&op) )
  {
    if ( op == 0x0B && r.pos == r.size )
      break;
    if ( op == 0x23 ) // global.get
    {
      uint64 g = 0;
      if ( !r.uleb(&g) )
        break;
      stack.push_back({ uint32(g), 0 });
    }
    else if ( op == 0x41 ) // i32.const
    {
      int64 c = 0;
      if ( !r.sleb33(&c) )
        break;
      stack.push_back({ UINT32_MAX, c });
    }
    else if ( op == 0x20 ) // local.get
    {
      uint64 l = 0;
      if ( !r.uleb(&l) )
        break;
      auto it = locals.find(uint32(l));
      stack.push_back(it != locals.end() ? it->second : got_val_t{ UINT32_MAX, 0 });
    }
    else if ( op == 0x21 ) // local.set
    {
      uint64 l = 0;
      if ( !r.uleb(&l) )
        break;
      if ( !stack.empty() )
      {
        locals[uint32(l)] = stack.back();
        stack.pop_back();
      }
    }
    else if ( op == 0x22 ) // local.tee
    {
      uint64 l = 0;
      if ( !r.uleb(&l) )
        break;
      if ( !stack.empty() )
      {
        locals[uint32(l)] = stack.back();
      }
    }
    else if ( op == 0x6A ) // i32.add
    {
      if ( stack.size() >= 2 )
      {
        got_val_t right = stack.back();
        stack.pop_back();
        got_val_t &left = stack.back();
        uint32 bg = left.base_global != UINT32_MAX ? left.base_global : right.base_global;
        left.base_global = bg;
        left.offset = left.offset + right.offset;
      }
    }
    else if ( op == 0x6B ) // i32.sub
    {
      if ( stack.size() >= 2 )
      {
        got_val_t right = stack.back();
        stack.pop_back();
        got_val_t &left = stack.back();
        uint32 bg = left.base_global != UINT32_MAX ? left.base_global : right.base_global;
        left.base_global = bg;
        left.offset = left.offset - right.offset;
      }
    }
    else if ( op == 0x24 ) // global.set
    {
      uint64 g = 0;
      if ( !r.uleb(&g) )
        break;
      if ( !stack.empty() )
      {
        got_val_t v = stack.back();
        stack.pop_back();
        if ( v.base_global == memory_base_index )
        {
          auto it = mem_offset_to_name.find(uint64(v.offset));
          if ( it != mem_offset_to_name.end() )
          {
            std::string got_name;
            got_name = format("GOT.mem.%s", it->second.c_str());
            (*got_names)[uint32(g)] = got_name;
          }
        }
        else if ( v.base_global == table_base_index )
        {
          uint32 slot = uint32(v.offset);
          auto it = elem_slot_to_func.find(slot);
          if ( it != elem_slot_to_func.end() )
          {
            auto fit = func_names.find(it->second);
            if ( fit != func_names.end() )
            {
              std::string got_name;
              got_name = format("GOT.func.%s", fit->second.c_str());
              (*got_names)[uint32(g)] = got_name;
            }
          }
        }
      }
    }
  }
}

// multi_result_word_count moved to ida/abi.cpp.

bool encode_reference_metadata(bytevec *encoded, const std::vector<int64> &heap_types,
                               const std::vector<uchar> &ref_forms)
{
  if ( heap_types.size() != ref_forms.size() )
    return false;
  if ( std::all_of(ref_forms.begin(), ref_forms.end(), [](uchar form) { return form == 0; }) )
    return true;
  append_uleb(encoded, ref_forms.size());
  for ( size_t i = 0; i < ref_forms.size(); ++i )
  {
    if ( ref_forms[i] > 2 || (ref_forms[i] != 0 && heap_types[i] == no_heap_type) )
      return false;
    encoded->push_back(ref_forms[i]);
    if ( ref_forms[i] != 0 )
    {
      const uint64 heap_type = uint64(heap_types[i]);
      for ( unsigned byte = 0; byte < 8; ++byte )
        encoded->push_back(uchar(heap_type >> (byte * 8)));
    }
  }
  return true;
}

#ifdef DOCTEST_LIBRARY_INCLUDED
namespace
{
std::vector<std::string> split_demangled_parameters(const std::string &text)
{
  std::vector<bool> after_operator(text.size(), false);
  for ( size_t i = 10; i < text.size(); ++i )
    if ( text.compare(i - 10, 2, "::") == 0 && text.compare(i - 8, 8, "operator") == 0 )
      after_operator[i] = true;
  auto continues_operator = [](char previous, char current)
  {
    return (previous == '<' && current == '<') || (previous == '>' && current == '>')
      || (previous == '(' && current == ')');
  };
  size_t depth = 0;
  bool in_parens = false;
  bool any_char = false;
  std::string current;
  std::vector<std::string> parameters;
  bool prev_operator_symbol = false;
  char prev_symbol = 0;
  for ( size_t i = 0; i < text.size(); ++i )
  {
    const char c = text[i];
    if ( c == '(' || c == ')' || c == '<' || c == '>' )
    {
      const bool operator_symbol
        = after_operator[i] || (prev_operator_symbol && continues_operator(prev_symbol, c));
      prev_operator_symbol = operator_symbol;
      prev_symbol = c;
      if ( operator_symbol )
        continue;
      if ( c == '(' )
      {
        if ( depth == 0 )
        {
          in_parens = true;
          parameters.clear();
          current.clear();
          any_char = false;
          ++depth;
          prev_operator_symbol = false;
          continue;
        }
        ++depth;
      }
      else if ( c == '<' )
        ++depth;
      else if ( depth > 0 )
        --depth;
      if ( in_parens && depth >= 1 )
      {
        current.push_back(c);
        any_char = true;
      }
      continue;
    }
    prev_operator_symbol = false;
    prev_symbol = 0;
    if ( !in_parens || depth < 1 )
      continue;
    if ( c == ',' && depth == 1 )
    {
      if ( any_char )
        parameters.push_back(current);
      current.clear();
      any_char = false;
      continue;
    }
    if ( c != ' ' )
    {
      any_char = true;
      current.push_back(c);
    }
  }
  if ( in_parens && any_char )
    parameters.push_back(current);
  if ( parameters.size() == 1 && parameters[0] == "void" )
    parameters.clear();
  return parameters;
}

std::string sanitize_opaque_struct_type(std::string text)
{
  if ( text.size() > 5 && text.compare(text.size() - 5, 5, "const") == 0 )
    text.resize(text.size() - 5);
  else if ( text.rfind("const", 0) == 0 && text.size() > 5 )
    text = text.substr(5);
  for ( char &c : text )
    if ( !(c == '_' || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')) )
      c = '_';
  while ( text.size() > 1 && text.front() == '_' )
    text.erase(0, 1);
  while ( text.size() > 1 && text.back() == '_' )
    text.pop_back();
  if ( text.empty()
       || !(text[0] == '_' || (text[0] >= 'a' && text[0] <= 'z')
            || (text[0] >= 'A' && text[0] <= 'Z')) )
    return std::string();
  return "struct " + text;
}

std::string mangled_param_type(const std::string &raw, uchar wasm_type)
{
  std::string text = raw;
  size_t references = 0;
  while ( !text.empty() && text.back() == '&' )
  {
    ++references;
    text.pop_back();
  }
  size_t pointers = references > 0 ? 1 : 0;
  while ( !text.empty() && text.back() == '*' )
  {
    ++pointers;
    text.pop_back();
  }
  if ( text.find('*') != std::string::npos || text.find('&') != std::string::npos )
  {
    size_t angle = 0;
    for ( char c : text )
    {
      if ( c == '<' )
        ++angle;
      else if ( c == '>' && angle > 0 )
        --angle;
      else if ( angle == 0 && (c == '*' || c == '&') )
        return std::string();
    }
  }
  if ( pointers == 0 && wasm_type == 0x7F )
  {
    std::string unqual = text;
    if ( unqual.size() > 5 && unqual.compare(unqual.size() - 5, 5, "const") == 0 )
      unqual.resize(unqual.size() - 5);
    else if ( unqual.rfind("const", 0) == 0 && unqual.size() > 5 )
      unqual = unqual.substr(5);
    const size_t opt_pos = unqual.find("optional_ptr<");
    const bool is_opt_ptr = opt_pos != std::string::npos
      && (opt_pos == 0 || (opt_pos >= 2 && unqual.compare(opt_pos - 2, 2, "::") == 0))
      && unqual.back() == '>';
    if ( is_opt_ptr )
    {
      const size_t inner_start = opt_pos + 13;
      const size_t inner_end = unqual.size() - 1;
      size_t depth = 0;
      size_t first_comma = std::string::npos;
      for ( size_t i = inner_start; i < inner_end; ++i )
      {
        if ( unqual[i] == '<' || unqual[i] == '(' )
          ++depth;
        else if ( (unqual[i] == '>' || unqual[i] == ')') && depth > 0 )
          --depth;
        else if ( unqual[i] == ',' && depth == 0 )
        {
          first_comma = i;
          break;
        }
      }
      const std::string inner = unqual.substr(
        inner_start, (first_comma != std::string::npos ? first_comma : inner_end) - inner_start);
      if ( !inner.empty() )
        return mangled_param_type(inner + "*", wasm_type);
    }
  }
  bool is_const = false;
  {
    std::string without_const;
    if ( text.rfind("const", 0) == 0 && text.size() > 5 )
      without_const = text.substr(5);
    else if ( text.size() > 5 && text.compare(text.size() - 5, 5, "const") == 0 )
      without_const = text.substr(0, text.size() - 5);
    if ( !without_const.empty() )
    {
      static const char *builtins[]
        = { "char",        "signedchar", "unsignedchar", "short",    "unsignedshort",    "int",
            "unsignedint", "long",       "unsignedlong", "longlong", "unsignedlonglong", "float",
            "double",      "bool",       "void" };
      for ( const char *builtin : builtins )
        if ( without_const == builtin )
        {
          is_const = true;
          text = without_const;
          break;
        }
    }
  }
  std::string base;
  if ( text == "char" || text == "signedchar" || text == "unsignedchar" || text == "short"
       || text == "unsignedshort" || text == "int" || text == "unsignedint" || text == "long"
       || text == "unsignedlong" || text == "longlong" || text == "unsignedlonglong"
       || text == "float" || text == "double" || text == "bool" || text == "void" )
    base = text;
  else
  {
    if ( pointers == 0 )
      return std::string();
    if ( text.size() > 5 && text.compare(text.size() - 5, 5, "const") == 0 )
      text.resize(text.size() - 5);
    else if ( text.rfind("const", 0) == 0 && text.size() > 5 )
      text = text.substr(5);
    const size_t template_open = text.find('<');
    if ( template_open != std::string::npos )
      text.resize(template_open);
    base = sanitize_opaque_struct_type(text);
    if ( base.empty() )
      return std::string();
  }
  std::string mapped;
  if ( base == "char" )
    mapped = is_const ? "const char" : "char";
  else if ( base == "signedchar" )
    mapped = "signed char";
  else if ( base == "unsignedchar" )
    mapped = is_const ? "const unsigned char" : "unsigned char";
  else if ( base == "short" )
    mapped = is_const ? "const short" : "short";
  else if ( base == "unsignedshort" )
    mapped = is_const ? "const unsigned short" : "unsigned short";
  else if ( base == "int" || base == "long" )
    mapped = is_const ? "const int" : "int";
  else if ( base == "unsignedint" || base == "unsignedlong" )
    mapped = is_const ? "const unsigned int" : "unsigned int";
  else if ( base == "longlong" )
    mapped = is_const ? "const __int64" : "__int64";
  else if ( base == "unsignedlonglong" )
    mapped = is_const ? "const unsigned __int64" : "unsigned __int64";
  else if ( base == "float" )
    mapped = is_const ? "const float" : "float";
  else if ( base == "double" )
    mapped = is_const ? "const double" : "double";
  else if ( base == "bool" )
    mapped = is_const ? "const int" : "int";
  else if ( base == "void" )
    mapped = pointers > 0 ? (is_const ? "const void" : "void") : std::string();
  else
    mapped = base;
  if ( mapped.empty() )
    return std::string();
  const bool wasm_i32 = wasm_type == 0x7F;
  const bool wasm_i64 = wasm_type == 0x7E;
  const bool wasm_f32 = wasm_type == 0x7D;
  const bool wasm_f64 = wasm_type == 0x7C;
  if ( pointers > 0 )
  {
    if ( !wasm_i32 )
      return std::string();
  }
  else if ( base == "char" || base == "signedchar" || base == "unsignedchar" || base == "short"
            || base == "unsignedshort" )
  {
    return std::string();
  }
  else if ( base == "int" || base == "long" || base == "unsignedint" || base == "unsignedlong"
            || base == "bool" )
  {
    if ( !wasm_i32 )
      return std::string();
  }
  else if ( base == "longlong" || base == "unsignedlonglong" )
  {
    if ( !wasm_i64 )
      return std::string();
  }
  else if ( base == "float" )
  {
    if ( !wasm_f32 )
      return std::string();
  }
  else if ( base == "double" )
  {
    if ( !wasm_f64 )
      return std::string();
  }
  else if ( base != "void" )
  {
    return std::string();
  }
  std::string result = mapped;
  for ( size_t i = 0; i < pointers; ++i )
    result += " *";
  return result;
}

std::string build_test_mangled_prototype(const std::string &demangled, const wasm_type_t &type)
{
  if ( type.results.size() > 1 || type.params.size() > 6
       || demangled.find('(') == std::string::npos )
    return std::string();
  const size_t paren = demangled.find('(');
  const std::string prefix = demangled.substr(0, paren);
  const std::vector<std::string> parameters = split_demangled_parameters(demangled);
  const bool has_scope = prefix.find("::") != std::string::npos;
  auto extract_this = [](const std::string &pref) -> std::string
  {
    const size_t sep = pref.rfind("::");
    if ( sep == std::string::npos )
      return std::string();
    std::string cls = pref.substr(0, sep);
    const size_t space = cls.rfind(' ');
    if ( space != std::string::npos )
      cls = cls.substr(space + 1);
    while ( !cls.empty() && (cls.front() == '*' || cls.front() == '&') )
      cls.erase(0, 1);
    const size_t tmpl = cls.find('<');
    if ( tmpl != std::string::npos )
      cls.resize(tmpl);
    for ( char &c : cls )
      if ( !(c == '_' || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')
             || (c >= '0' && c <= '9')) )
        c = '_';
    return (cls.empty() || cls == "operator") ? std::string() : ("struct " + cls + " *");
  };
  bool has_sret = false;
  bool is_method = false;
  std::vector<std::string> param_types;
  if ( parameters.size() == type.params.size() )
  {
    if ( parameters.empty() )
      return std::string();
    param_types = parameters;
  }
  else if ( parameters.size() + 1 == type.params.size() )
  {
    if ( type.params[0] != 0x7F )
      return std::string();
    if ( has_scope )
    {
      const std::string this_t = extract_this(prefix);
      if ( this_t.empty() )
        return std::string();
      is_method = true;
      param_types.push_back(this_t);
      param_types.insert(param_types.end(), parameters.begin(), parameters.end());
    }
    else
    {
      if ( !type.results.empty() )
        return std::string();
      has_sret = true;
      param_types.push_back("void *");
      param_types.insert(param_types.end(), parameters.begin(), parameters.end());
    }
  }
  else if ( parameters.size() + 2 == type.params.size() && has_scope && type.results.empty() )
  {
    if ( type.params[0] != 0x7F || type.params[1] != 0x7F )
      return std::string();
    const std::string this_t = extract_this(prefix);
    if ( this_t.empty() )
      return std::string();
    has_sret = true;
    is_method = true;
    param_types.push_back("void *");
    param_types.push_back(this_t);
    param_types.insert(param_types.end(), parameters.begin(), parameters.end());
  }
  else
  {
    return std::string();
  }
  std::string out = type.results.empty() ? "void" : c_type(type.results[0]);
  out += "(";
  for ( size_t i = 0; i < param_types.size(); ++i )
  {
    std::string mapped;
    if ( has_sret && i == 0 )
      mapped = "void *";
    else if ( is_method && i == (has_sret ? 1 : 0) )
      mapped = param_types[i];
    else
    {
      mapped = mangled_param_type(param_types[i], type.params[i]);
      if ( mapped.empty() )
        mapped = c_type(type.params[i]);
    }
    std::string arg_name = format("a%zu", i + 1);
    if ( has_sret && i == 0 )
      arg_name = "__ret";
    else if ( is_method && i == (has_sret ? 1 : 0) )
      arg_name = "this";
    if ( i != 0 )
      out += ", ";
    out += mapped + " " + arg_name;
  }
  out += ")";
  return out;
}
} // namespace

TEST_CASE("M4-S1: per-param mapping, optional_ptr, sret, and multi-export ICF consistency")
{
  // 1. split_demangled_parameters preserves multi-arg templates at depth > 1
  const std::string extract_dem
    = "duckdb::RelationManager::ExtractJoinRelations("
      "duckdb::JoinOrderOptimizer&, duckdb::LogicalOperator&, "
      "duckdb::vector<std::__2::reference_wrapper<duckdb::LogicalOperator>, true>&, "
      "duckdb::optional_ptr<duckdb::LogicalOperator, true>)";
  const std::vector<std::string> split_extract = split_demangled_parameters(extract_dem);
  REQUIRE(split_extract.size() == 4);
  CHECK(split_extract[3] == "duckdb::optional_ptr<duckdb::LogicalOperator,true>");

  // 2. mangled_param_type: optional_ptr<T, true> -> struct T *, enum -> empty (fallback to int)
  CHECK(mangled_param_type(split_extract[0], 0x7F) == "struct duckdb__JoinOrderOptimizer *");
  CHECK(mangled_param_type(split_extract[1], 0x7F) == "struct duckdb__LogicalOperator *");
  CHECK(mangled_param_type(split_extract[3], 0x7F) == "struct duckdb__LogicalOperator *");
  CHECK(mangled_param_type("duckdb::TaskExecutionMode", 0x7F).empty());

  // 3. Per-param mapping on ExtractJoinRelations, AddRelation, ExecuteTask
  wasm_type_t extract_sig;
  extract_sig.params = { 0x7F, 0x7F, 0x7F, 0x7F, 0x7F };
  extract_sig.results = { 0x7F };
  CHECK(build_test_mangled_prototype(extract_dem, extract_sig)
        == "int(struct duckdb__RelationManager * this, "
           "struct duckdb__JoinOrderOptimizer * a2, "
           "struct duckdb__LogicalOperator * a3, "
           "struct duckdb__vector * a4, "
           "struct duckdb__LogicalOperator * a5)");

  wasm_type_t exec_sig;
  exec_sig.params = { 0x7F, 0x7F };
  exec_sig.results = { 0x7F };
  const std::string exec_dem = "duckdb::PipelineTask::ExecuteTask(duckdb::TaskExecutionMode)";
  CHECK(build_test_mangled_prototype(exec_dem, exec_sig)
        == "int(struct duckdb__PipelineTask * this, int a2)");

  // 4. Hidden sret parameter (method +2 and free function +1)
  wasm_type_t method_sret_sig;
  method_sret_sig.params = { 0x7F, 0x7F, 0x7F };
  CHECK(
    build_test_mangled_prototype(
      "arrow::Field::WithType(std::__2::shared_ptr<arrow::DataType> const&) const", method_sret_sig)
    == "void(void * __ret, struct arrow__Field * this, struct std____2__shared_ptr * a3)");

  wasm_type_t free_sret_sig;
  free_sret_sig.params = { 0x7F, 0x7F, 0x7F };
  CHECK(build_test_mangled_prototype("ErrorGetDetail(AdbcError const*, int)", free_sret_sig)
        == "void(void * __ret, struct AdbcError * a2, int a3)");

  // 5. Multi-export representative consistency between function_name() and resolve_got_globals()
  module_t mod;
  mod.globals.resize(2);
  mod.globals[0].imported = true;
  mod.globals[0].module_name = "env";
  mod.globals[0].field_name = "__table_base";
  mod.globals[0].type = 0x7F;
  mod.globals[1].imported = false;
  mod.globals[1].type = 0x7F;
  mod.elem_entries.push_back({ 5u, 3479u });
  mod.function_exports.push_back(
    { "_ZNK6duckdb12optional_ptrINS_15JoinRelationSetELb1EE10CheckValidEv", 3479u });
  mod.function_exports.push_back(
    { "_ZNK6duckdb12optional_ptrINS_14NodeStatisticsELb1EE10CheckValidEv", 3479u });
  // Start body: global.get 0; i32.const 5; i32.add; global.set 1; end
  const uchar start_code[] = { 0x00, 0x23, 0x00, 0x41, 0x05, 0x6A, 0x24, 0x01, 0x0B };
  mod.start_function_body = reader_t{ start_code, sizeof(start_code), 0 };

  std::unordered_set<std::string> used;
  const std::string rep_name
    = function_name(3479u, mod.function_exports, mod.function_names, &used);
  std::unordered_map<uint32, std::string> got_names;
  resolve_got_globals(mod, &got_names);
  CHECK(rep_name == "_ZNK6duckdb12optional_ptrINS_15JoinRelationSetELb1EE10CheckValidEv");
  REQUIRE(got_names.count(1u) == 1);
  CHECK(got_names[1u] == "GOT.func." + rep_name);
}
#endif
}
