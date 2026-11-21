#include "ctree_types.hpp"

#include <algorithm>
#include <cstring>
#include <map>
#include <typeinf.hpp>

namespace wasmhex
{

namespace
{

struct access_t
{
  uint32 offset;
  int size;
  bool is_float;
};

class deref_collector_t final : public ctree_visitor_t
{
public:
  // lvar index -> offset -> access; conflicting accesses remove the lvar.
  std::map<int, std::map<uint32, access_t>> accesses;
  std::set<int> conflicting;

  deref_collector_t() : ctree_visitor_t(CV_POST) {}

  static const cexpr_t *strip_cast(const cexpr_t *e)
  {
    while ( e != nullptr && e->op == cot_cast && e->x != nullptr )
      e = e->x;
    return e;
  }

  int idaapi visit_expr(cexpr_t *expression) override
  {
    if ( expression->op != cot_ptr || expression->x == nullptr )
      return 0;
    const cexpr_t *base = strip_cast(expression->x);
    uint32 offset = 0;
    if ( base->op == cot_add )
    {
      const cexpr_t *lhs = strip_cast(base->x);
      const cexpr_t *rhs = strip_cast(base->y);
      if ( lhs == nullptr || rhs == nullptr )
        return 0;
      if ( lhs->op == cot_var && rhs->op == cot_num )
      {
        base = lhs;
        offset = uint32(rhs->numval());
      }
      else if ( rhs->op == cot_var && lhs->op == cot_num )
      {
        base = rhs;
        offset = uint32(lhs->numval());
      }
      else
        return 0;
    }
    if ( base == nullptr || base->op != cot_var )
      return 0;
    const tinfo_t &value_type = expression->type;
    const int size = value_type.is_void() ? 0 : int(value_type.get_size());
    if ( size != 1 && size != 2 && size != 4 && size != 8 )
      return 0;
    if ( offset > 0x100000 )
      return 0;
    const int index = int(base->v.idx);
    const access_t access = { offset, size, value_type.is_floating() };
    auto &by_offset = accesses[index];
    const auto found = by_offset.find(offset);
    if ( found != by_offset.end()
         && (found->second.size != access.size || found->second.is_float != access.is_float) )
    {
      // An 8-byte integer copy plus 4-byte accesses at the same
      // offset is the classic "struct copied through a QWORD" shape
      // (LLVM wasm32 memcpy of a 2x4 struct). Split the wide access into
      // its two dwords instead of failing the whole local - safe on
      // non-shared memory, and the struct inference then renders the
      // field accesses.
      const bool wide_int_copy = !access.is_float && !found->second.is_float
        && ((found->second.size == 8 && access.size == 4)
            || (found->second.size == 4 && access.size == 8));
      if ( wide_int_copy )
      {
        found->second = access_t{ offset, 4, false };
        const uint32 high_offset = offset + 4;
        const auto high = by_offset.find(high_offset);
        if ( high != by_offset.end() && (high->second.size != 4 || high->second.is_float) )
        {
          conflicting.insert(index);
          return 0;
        }
        by_offset[high_offset] = access_t{ high_offset, 4, false };
        return 0;
      }
      conflicting.insert(index);
      return 0;
    }
    by_offset[offset] = access;
    return 0;
  }
};

tinfo_t parse_scalar_type(const char *declaration)
{
  tinfo_t type;
  if ( !type.parse(declaration, get_idati(), PT_SIL) )
    return tinfo_t();
  return type;
}

bool scalar_type(int size, bool is_float, tinfo_t *out)
{
  static const tinfo_t char_type = parse_scalar_type("char x;");
  static const tinfo_t short_type = parse_scalar_type("short x;");
  static const tinfo_t int_type = parse_scalar_type("int x;");
  static const tinfo_t int64_type = parse_scalar_type("__int64 x;");
  static const tinfo_t float_type = parse_scalar_type("float x;");
  static const tinfo_t double_type = parse_scalar_type("double x;");
  if ( is_float )
    *out = size == 4 ? float_type : size == 8 ? double_type : tinfo_t();
  else
    *out = size == 1 ? char_type
      : size == 2    ? short_type
      : size == 4    ? int_type
      : size == 8    ? int64_type
                     : tinfo_t();
  return !out->empty();
}

bool scalar_pointer(const access_t &access, tinfo_t *out)
{
  tinfo_t element;
  if ( !scalar_type(access.size, access.is_float, &element) )
    return false;
  return out->create_ptr(element);
}

bool build_struct_pointer(const std::map<uint32, access_t> &by_offset, const qstring &name,
                          tinfo_t *out)
{
  udt_type_data_t udt;
  udt.effalign = 1;
  udt.taudt_bits |= TAUDT_UNALIGNED;
  uint32 cursor = 0;
  for ( const auto &entry : by_offset )
  {
    const access_t &access = entry.second;
    if ( entry.first < cursor )
      return false; // overlapping accesses, refuse to guess
    if ( entry.first > cursor )
    {
      udm_t pad;
      pad.name.sprnt("pad_%u", cursor);
      tinfo_t element;
      if ( !scalar_type(1, false, &element)
           || !pad.type.create_array(element, entry.first - cursor) )
        return false;
      pad.offset = uint64(cursor) * 8;
      udt.push_back(pad);
    }
    udm_t member;
    member.name.sprnt("field_%u", entry.first);
    if ( !scalar_type(access.size, access.is_float, &member.type) )
      return false;
    member.offset = uint64(entry.first) * 8;
    udt.push_back(member);
    cursor = entry.first + uint32(access.size);
  }
  udt.total_size = cursor;
  tinfo_t struct_type;
  if ( !struct_type.create_udt(udt) )
    return false;
  if ( struct_type.set_named_type(get_idati(), name.c_str(), NTF_REPLACE) != TERR_OK )
    return false;
  tinfo_t named;
  if ( !named.get_named_type(get_idati(), name.c_str()) )
    return false;
  return out->create_ptr(named);
}

class inference_applier_t final : public user_lvar_modifier_t
{
public:
  struct candidate_t
  {
    lvar_locator_t locator;
    tinfo_t type;
  };
  const qvector<candidate_t> &candidates;

  explicit inference_applier_t(const qvector<candidate_t> &_candidates) : candidates(_candidates) {}

  bool idaapi modify_lvars(lvar_uservec_t *lvinf) override
  {
    for ( const candidate_t &candidate : candidates )
    {
      const auto end = lvinf->lvvec.end();
      const auto found
        = std::find_if(lvinf->lvvec.begin(), end, [&candidate](const lvar_saved_info_t &info)
                       { return info.ll == candidate.locator; });
      if ( found != end )
        continue; // never overwrite existing user info
      lvar_saved_info_t info;
      info.ll = candidate.locator;
      info.type = candidate.type;
      lvinf->lvvec.push_back(info);
    }
    return true;
  }
};

} // anonymous namespace

void apply_struct_pointer_inference(cfunc_t *cfunc)
{
  deref_collector_t collector;
  collector.apply_to(&cfunc->body, nullptr);

  lvars_t *lvars = cfunc->get_lvars();
  if ( lvars == nullptr || lvars->empty() )
    return;

  qvector<inference_applier_t::candidate_t> candidates;
  for ( const auto &entry : collector.accesses )
  {
    const int index = entry.first;
    if ( index < 0 || size_t(index) >= lvars->size() )
      continue;
    if ( collector.conflicting.find(index) != collector.conflicting.end() )
      continue;
    const lvar_t &lvar = (*lvars)[size_t(index)];
    // Only plain integer locals of pointer width are candidates; locals that
    // already carry a pointer-to-udt type are done (idempotency, and anything
    // the user set by hand wins).
    const tinfo_t current = lvar.type();
    if ( !current.is_integral() || current.get_size() != 4 )
      continue;
    if ( current.is_ptr() )
    {
      tinfo_t pointee = current.get_ptrarr_object();
      if ( pointee.is_udt() )
        continue;
    }
    tinfo_t pointer;
    if ( entry.second.size() >= 2 )
    {
      qstring name;
      name.sprnt("wasm_%a_v%u", cfunc->entry_ea, uint32(index));
      if ( !build_struct_pointer(entry.second, name, &pointer) )
        continue;
    }
    else
    {
      if ( !scalar_pointer(entry.second.begin()->second, &pointer) )
        continue;
    }
    inference_applier_t::candidate_t candidate;
    candidate.locator = lvar;
    candidate.type = pointer;
    candidates.push_back(candidate);
  }

  if ( candidates.empty() )
    return;
  inference_applier_t applier(candidates);
  modify_user_lvars(cfunc->entry_ea, applier);
}

// The shared string-literal builder (the rewriter_idiom
// __memory_base path and the Go call-argument swap render the same shape:
// cot_str + EXFL_CSTR + char[len+1]).
// EA is the literal's ANCHOR and must be the linear-memory DATA EA the
// string bytes live at — never an instruction/call-site EA. Hex-Rays
// persists every final ctree string literal into the IDB's
// synthetic-string registry (kernel `synth_string_set`,
// SYNTHSTR_PROVIDER_DECOMPILER) at the literal's EA, and the Strings
// window lists those entries; a call-site anchor pollutes it with .text
// "Decompiler" rows. At the data EA the entry lands on the item the
// loader already made (dref kind-4 / strlit), so no new item is needed —
// the builder never mutates the IDB.
cexpr_t *make_go_string_literal(const qstring &text, ea_t ea)
{
  cexpr_t *literal = new cexpr_t;
  literal->op = cot_str;
  literal->string = qstrdup(text.c_str());
  literal->exflags |= EXFL_CSTR;
  literal->ea = ea;
  tinfo_t char_type = tinfo_t(BT_INT8); // char == signed byte on wasm targets
  literal->type.create_array(char_type, asize_t(text.length() + 1));
  return literal;
}

// C string args: `&item` — cot_ref over cot_obj. The type fields mirror what
// the engine builds for a real address-of (item type + pointer to it) so the
// printer never sees an empty type; the swap is display-only, so the
// int-typed call argument position keeps its own signature type.
cexpr_t *make_data_address_literal(ea_t ea)
{
  tinfo_t item_type;
  if ( !get_tinfo(&item_type, ea) || item_type.empty() )
  {
    tinfo_t char_type = tinfo_t(BT_INT8);
    item_type.create_array(char_type, asize_t(1)); // unnamed byte item
  }
  cexpr_t *object = new cexpr_t;
  object->op = cot_obj;
  object->obj_ea = ea;
  object->type = item_type;
  object->ea = ea;
  cexpr_t *reference = new cexpr_t;
  reference->op = cot_ref;
  reference->x = object;
  reference->type.create_ptr(item_type);
  reference->ea = ea;
  return reference;
}

} // namespace wasmhex
