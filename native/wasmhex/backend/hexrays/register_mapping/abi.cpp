#include "abi.hpp"

namespace wasmhex
{
extern "C" __attribute__((used)) const char wasmhex_s4_build_marker[]
  = "WASMHEX_BUILD_MARKER_M4_S4_CFRAME_SPLIT_KREG_LIBCXX";

extern "C" __attribute__((used)) const char wasmhex_abi_audit_marker[]
  = "WASMHEX_BUILD_MARKER_ABI_AUDIT_8B_RETURN_ENGINE_LIMIT";

bool get_wasm_argument_register(size_t word_index, int *reg)
{
  const size_t mapped = word_index < 4 ? word_index : word_index + 2;
  if ( reg == nullptr || mapped >= size_t(wasmhex_user_regs) )
    return false;
  *reg = int(mapped);
  return true;
}

bool wasm_argument_on_stack(size_t word_index, int size)
{
  return word_index >= 6 || (size == 8 && word_index + 1 >= 6);
}

size_t wasm_argument_word_count(size_t word_index, int size)
{
  return size_t(size) / 4 + (wasm_argument_on_stack(word_index, size) && word_index < 6 ? 1 : 0);
}

bool make_wasm_argument_location(size_t word_index, int size, argloc_t *location)
{
  int low = -1;
  if ( location == nullptr || (size != 4 && size != 8) )
    return false;
  if ( wasm_argument_on_stack(word_index, size) )
  {
    location->set_stkoff(word_index < 6 ? 0 : sval_t(word_index - 6) * 4);
    return true;
  }
  if ( !get_wasm_argument_register(word_index, &low) )
    return false;
  if ( size == 4 )
  {
    location->set_reg1(low);
    return true;
  }
  int high = -1;
  if ( !get_wasm_argument_register(word_index + 1, &high) )
    return false;
  location->set_reg2(low, high);
  return true;
}

bool append_wasm_user_argument(qstring *declaration, const char *type, const char *name,
                               size_t word_index, int size, bool comma)
{
  int low = -1;
  if ( declaration == nullptr || type == nullptr || name == nullptr || (size != 4 && size != 8) )
    return false;
  qstring argument;
  if ( wasm_argument_on_stack(word_index, size) )
  {
    argument.sprnt("%s %s", type, name);
  }
  else
  {
    if ( !get_wasm_argument_register(word_index, &low) )
      return false;
    argument.sprnt("%s %s@<r%d", type, name, low);
    if ( size == 8 )
    {
      int high = -1;
      if ( !get_wasm_argument_register(word_index + 1, &high) )
        return false;
      qstring second_register;
      second_register.sprnt(":r%d", high);
      argument.append(second_register);
    }
    argument.append(">");
  }
  if ( comma )
    declaration->append(", ");
  declaration->append(argument);
  return true;
}

// Shared multi-result ABI helpers. These used to exist twice — one
// copy for the loader (loader/database.cpp, core/module.cpp) and one for the
// processor module (ida/metadata.cpp) — with slightly different wording for
// the same logic. The word accounting for i64/f64 results is the part that
// must never drift, so the processor variants (built on wasm_type_size) are
// now the single source. Note the type whitelist inside
// multi_result_word_count rejects v128 and exotic references before any
// division happens, so wasm_type_size()/4 and the old "?2:1" wording agree
// on every accepted type.
int wasm_type_size(uchar type)
{
  switch ( type )
  {
    case 0x7F:
    case 0x7D:
    case 0x70:
    case 0x6F:
    case 0x71:
    case 0x72: return 4;
    case 0x7E:
    case 0x7C: return 8;
    case 0x7B: return 16;
    default: return 0;
  }
}

qstring tuple_type_name(const std::vector<uchar> &results)
{
  qstring name("wasm_tuple");
  for ( uchar type : results )
  {
    qstring part;
    part.sprnt("_%02x", type);
    name.append(part);
  }
  return name;
}

bool multi_result_word_count(const std::vector<uchar> &results, size_t *words, bool allow_sret)
{
  if ( results.size() < 2 )
    return false;
  size_t count = 0;
  size_t reference_count = 0;
  for ( uchar type : results )
  {
    if ( type != 0x7F && type != 0x7E && type != 0x7D && type != 0x7C && type != 0x70
         && type != 0x6F )
      return false;
    if ( (type == 0x70 || type == 0x6F) && ++reference_count > 1 )
      return false;
    count += wasm_type_size(type) / 4;
    if ( count > 6 && !allow_sret )
      return false;
  }
  *words = count;
  return true;
}

bool tuple_layout_matches(const tinfo_t &tuple_type, const std::vector<uchar> &results)
{
  udt_type_data_t members;
  if ( !tuple_type.is_tuple() || !tuple_type.get_udt_details(&members)
       || members.size() != results.size() )
    return false;
  size_t offset = 0;
  for ( size_t i = 0; i < results.size(); ++i )
  {
    const int size = wasm_type_size(results[i]);
    const udm_t &member = members[i];
    if ( member.offset != offset * 8 || member.type.get_size() != size
         || member.type.is_floating() != (results[i] == 0x7D || results[i] == 0x7C) )
      return false;
    offset += size;
  }
  return tuple_type.get_size() == offset;
}

bool get_multi_result_type(const std::vector<uchar> &results, tinfo_t *tuple_type)
{
  size_t words = 0;
  if ( !multi_result_word_count(results, &words, false) )
    return false;
  (void)words;
  const qstring name = tuple_type_name(results);
  if ( tuple_type->get_named_type(get_idati(), name.c_str(), BTF_STRUCT) )
    return tuple_layout_matches(*tuple_type, results);

  qstring declaration;
  declaration.sprnt("#pragma pack(push, 4)\n__tuple %s { ", name.c_str());
  for ( size_t i = 0; i < results.size(); ++i )
  {
    // multi_result_word_count already whitelisted the result types, so the
    // mapping below is exhaustive for every accepted type.
    qstring field;
    field.sprnt("%s _r%zu; ",
                results[i] == 0x7F     ? "int"
                  : results[i] == 0x7E ? "__int64"
                  : results[i] == 0x7D ? "float"
                  : results[i] == 0x7C ? "double"
                                       : "unsigned int",
                i);
    declaration.append(field);
  }
  declaration.append("};\n#pragma pack(pop)");
  return parse_decls(get_idati(), declaration.c_str(), nullptr, HTI_DCL) == 0
    && tuple_type->get_named_type(get_idati(), name.c_str(), BTF_STRUCT)
    && tuple_layout_matches(*tuple_type, results);
}

bool make_multi_return_location(const std::vector<uchar> &results, argloc_t *return_location)
{
  size_t words = 0;
  if ( !multi_result_word_count(results, &words, false) )
    return false;
  (void)words;
  scattered_aloc_t *parts = new scattered_aloc_t;
  if ( parts == nullptr )
    return false;
  size_t offset = 0;
  size_t reg = 0;
  for ( uchar type : results )
  {
    const size_t size = wasm_type_size(type);
    argpart_t &part = parts->push_back();
    part.off = ushort(offset);
    part.size = ushort(size);
    if ( size == 4 )
      part.set_reg1(multi_return_registers[reg]);
    else
      part.set_reg2(multi_return_registers[reg], multi_return_registers[reg + 1]);
    offset += size;
    reg += size / 4;
  }
  return_location->consume_scattered(parts);
  rangeset_t gaps;
  return optimize_argloc(return_location, int(offset), &gaps);
}

}
