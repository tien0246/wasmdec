#include <cstddef>
#include <cstdint>
#include <cstring>
#include <unistd.h>

namespace wasmhex
{
namespace libcxx_detail
{

struct libcxx_match_stats_t
{
  std::size_t matched[4] = { 0, 0, 0, 0 };
  std::size_t skipped[4] = { 0, 0, 0, 0 };
  // Telemetry: how many of the matched/skipped SSO sites carried
  // a loader mark (recognition at the IR) vs ran the legacy name path.
  std::size_t sso_mark_hits = 0;
};

// unique_ptr virtual delete through vtable slot +4:
//   if ( p != 0 )
//     __indirect_function_table[*(_DWORD *)(*(_DWORD *)p + 4)](p);
// -> delete(p); // std::default_delete
struct unique_ptr_delete_candidate_t
{
  bool has_null_guard = false;
  bool has_else = false;
  bool is_indirect_table_call = false;
  bool index_reads_vtable_from_ptr = false;
  std::uint64_t vtable_byte_offset = 0;
  std::size_t arg_count = 0;
  int cond_lvar = -1;
  int vtable_obj_lvar = -1;
  int call_arg_lvar = -1;
  bool inner_stmt_has_external_label = false;
};

inline bool verify_unique_ptr_delete_site(const unique_ptr_delete_candidate_t &c)
{
  return c.has_null_guard && !c.has_else && c.is_indirect_table_call
    && c.index_reads_vtable_from_ptr && c.vtable_byte_offset == 4 && c.arg_count == 1
    && c.cond_lvar >= 0 && c.cond_lvar == c.vtable_obj_lvar && c.cond_lvar == c.call_arg_lvar
    && !c.inner_stmt_has_external_label;
}

// libc++ std::string SSO destructor:
//   Pointer form: if ( *(char *)(s + 11) < 0 ) free(*(void **)s);
//   Stack form:   if ( s_tag < 0 ) free(s_data);   (stkoff(s_tag) - stkoff(s_data) == 11 for 1B or 8 for 4B)
// -> std::__1::string::~string(s)
struct sso_string_dtor_candidate_t
{
  bool has_else = false;
  bool inner_stmt_has_external_label = false;
  bool is_signed_lt_zero = false;
  bool is_free_call = false;
  std::size_t free_arg_count = 0;
  bool is_pointer_form = false;
  bool is_stack_form = false;
  int tag_read_size = 0;
  int data_read_size = 0;
  std::int64_t tag_byte_offset = 0;
  std::int64_t data_byte_offset = 0;
  bool same_base_expr = false;
  std::int64_t tag_stkoff = 0;
  std::int64_t data_stkoff = 0;
  bool mark_proven = false;
};

inline bool verify_sso_string_dtor_site(const sso_string_dtor_candidate_t &c)
{
  if ( c.has_else || c.inner_stmt_has_external_label || !c.is_signed_lt_zero || !c.is_free_call
       || c.free_arg_count != 1 || c.data_read_size != 4 )
    return false;
  if ( c.mark_proven )
    return (c.is_pointer_form && c.same_base_expr) || c.is_stack_form;
  if ( c.is_pointer_form && !c.is_stack_form )
  {
    const bool valid_tag = (c.tag_read_size == 1 && c.tag_byte_offset == 11)
      || (c.tag_read_size == 4 && c.tag_byte_offset == 8);
    return valid_tag && c.data_byte_offset == 0 && c.same_base_expr;
  }
  if ( c.is_stack_form && !c.is_pointer_form )
  {
    const std::int64_t delta = c.tag_stkoff - c.data_stkoff;
    return (c.tag_read_size == 1 && delta == 11) || (c.tag_read_size == 4 && delta == 8);
  }
  return false;
}

// libc++ std::vector<std::string> destructor loop:
//   v_beg = v.begin;
//   if ( v.begin != nullptr ) {
//     v_free = v.begin;
//     v_it = v.end;
//     if ( v.begin != v.end ) {
//       do {
//         v_prev = v_it - 3; // -12 bytes
//         if ( *((char *)v_it - 1) < 0 ) free(*v_prev);
//         v_it -= 3;         // -12 bytes
//       } while ( v_prev != v_beg );
//       v_free = v.begin;
//     }
//     v.end = v_beg;
//     free(v_free);
//   }
// -> std::vector<std::string>::~vector(v)
struct vector_string_dtor_candidate_t
{
  std::int64_t end_minus_begin_bytes = 0;
  bool has_outer_nonnull_check = false;
  bool has_begin_ne_end_check = false;
  bool has_do_loop = false;
  std::int64_t step_prev_bytes = 0;
  std::int64_t tag_offset_from_it = 0;
  int tag_read_size = 0;
  bool tag_is_signed_lt_zero = false;
  bool calls_free_on_prev_deref = false;
  std::int64_t iter_decrement_bytes = 0;
  bool loop_cond_compares_prev_to_begin = false;
  bool resets_end_to_begin = false;
  bool calls_free_on_begin = false;
  bool has_external_incoming_label = false;
  bool temps_do_not_escape = false;
};

inline bool verify_vector_string_dtor_site(const vector_string_dtor_candidate_t &c)
{
  return c.end_minus_begin_bytes == 4 && c.has_outer_nonnull_check && c.has_begin_ne_end_check
    && c.has_do_loop && c.step_prev_bytes == -12 && c.tag_offset_from_it == -1
    && c.tag_read_size == 1 && c.tag_is_signed_lt_zero && c.calls_free_on_prev_deref
    && c.iter_decrement_bytes == 12 && c.loop_cond_compares_prev_to_begin && c.resets_end_to_begin
    && c.calls_free_on_begin && !c.has_external_incoming_label && c.temps_do_not_escape;
}

// libc++ std::vector<T>::push_back inlined reallocation (~55 lines):
//   capacity check (end >= cap or end < cap), __throw_length_error,
//   __cxa_throw_bad_array_new_length, _Znwm, new element store, backward
//   copy loop, {begin, end, cap} update, free(old_begin), final end store
// -> std::vector<T>::push_back(&vec, val)
struct vector_push_back_candidate_t
{
  std::int64_t end_offset_from_begin = 0;
  std::int64_t cap_offset_from_begin = 0;
  std::uint64_t elem_size = 0;
  std::uint64_t max_size_constant = 0;
  bool has_capacity_check = false;
  bool has_fast_path_store_and_advance = false;
  bool has_throw_length_error = false;
  bool has_bad_array_new_length = false;
  bool has_operator_new = false;
  bool has_new_elem_store = false;
  bool has_backward_copy_loop = false;
  bool has_vector_triple_update = false;
  bool has_old_buffer_free = false;
  bool has_final_end_store = false;
  bool has_external_incoming_label = false;
  bool temps_do_not_escape = false;
};

inline bool verify_vector_push_back_realloc_site(const vector_push_back_candidate_t &c)
{
  if ( c.end_offset_from_begin != 4 || c.cap_offset_from_begin != 8 || c.elem_size == 0
       || (c.elem_size & (c.elem_size - 1)) != 0 || c.elem_size > 0x40000000ULL )
    return false;
  const std::uint64_t expected_max_size = 0x100000000ULL / c.elem_size;
  if ( c.max_size_constant != expected_max_size )
    return false;
  return c.has_capacity_check && c.has_fast_path_store_and_advance && c.has_throw_length_error
    && c.has_bad_array_new_length && c.has_operator_new && c.has_new_elem_store
    && c.has_backward_copy_loop && c.has_vector_triple_update && c.has_old_buffer_free
    && c.has_final_end_store && !c.has_external_incoming_label && c.temps_do_not_escape;
}

} // namespace libcxx_detail
} // namespace wasmhex

#ifndef WASMHEX_PURE_STRING_TEST

#include "ctree.hpp"
#include "../../../ida/loader/options.hpp"

#include <idp.hpp>
#include <name.hpp>
#include <typeinf.hpp>

#include <map>
#include <set>
#include <vector>

namespace wasmhex
{

namespace
{

const cexpr_t *strip_casts(const cexpr_t *e)
{
  while ( e != nullptr && e->op == cot_cast && e->x != nullptr )
    e = e->x;
  return e;
}

cblock_t::iterator next_insn_it(cblock_t::iterator it)
{
  ++it;
  return it;
}

cblock_t::iterator prev_insn_it(cblock_t::iterator it)
{
  --it;
  return it;
}

cexpr_t *strip_casts_mut(cexpr_t *e)
{
  while ( e != nullptr && e->op == cot_cast && e->x != nullptr )
    e = e->x;
  return e;
}

const cexpr_t *strip_wrappers(const cexpr_t *e)
{
  e = strip_casts(e);
  while ( e != nullptr && (e->op == cot_ref || e->op == cot_ptr) && e->x != nullptr )
    e = strip_casts(e->x);
  return e;
}

bool callee_name(const cexpr_t *call_expr, qstring *out)
{
  if ( call_expr == nullptr || call_expr->op != cot_call || call_expr->x == nullptr )
    return false;
  const cexpr_t *callee = strip_wrappers(call_expr->x);
  if ( callee == nullptr )
    return false;
  if ( callee->op == cot_obj )
  {
    get_name(out, callee->obj_ea);
    return true;
  }
  if ( callee->op == cot_helper && callee->helper != nullptr )
  {
    *out = callee->helper;
    return true;
  }
  return false;
}

bool is_free_callee_name(const char *name)
{
  if ( name == nullptr )
    return false;
  while ( *name == '_' )
    ++name;
  return strcmp(name, "free") == 0;
}

bool is_free_call_expr(const cexpr_t *expr, const cexpr_t **out_arg = nullptr)
{
  expr = strip_casts(expr);
  if ( expr == nullptr || expr->op != cot_call || expr->a == nullptr || expr->a->size() != 1 )
    return false;
  qstring name;
  if ( !callee_name(expr, &name) || !is_free_callee_name(name.c_str()) )
    return false;
  if ( out_arg != nullptr )
    *out_arg = &(*expr->a)[0];
  return true;
}

bool is_free_call_stmt(const cinsn_t &stmt, const cexpr_t **out_arg = nullptr)
{
  return stmt.op == cit_expr && stmt.cexpr != nullptr && is_free_call_expr(stmt.cexpr, out_arg);
}

bool is_indirect_table_expr(const cexpr_t *e)
{
  e = strip_casts(e);
  return e != nullptr && e->op == cot_idx && e->x != nullptr && e->x->op == cot_helper
    && e->x->helper != nullptr && strcmp(e->x->helper, "__indirect_function_table") == 0
    && e->y != nullptr;
}

size_t pointed_elem_size(const tinfo_t &tif)
{
  if ( !tif.is_ptr() )
    return 1;
  const size_t sz = tif.get_pointed_object().get_size();
  return (sz == 0 || sz == BADSIZE) ? 1 : sz;
}

// Decompose `base + const` or `base - const` (allowing casts around base or
// the whole expression) into (base_expr, signed_byte_offset).
bool decompose_addr_offset(const cexpr_t *addr, const cexpr_t **out_base, int64 *out_byte_off)
{
  if ( addr == nullptr || out_base == nullptr || out_byte_off == nullptr )
    return false;
  const cexpr_t *cur = strip_casts(addr);
  if ( cur == nullptr )
    return false;
  if ( (cur->op == cot_add || cur->op == cot_sub) && cur->x != nullptr && cur->y != nullptr )
  {
    const size_t scale = pointed_elem_size(cur->type);
    const cexpr_t *cx = strip_casts(cur->x);
    const cexpr_t *cy = strip_casts(cur->y);
    if ( cx != nullptr && cy != nullptr && cy->op == cot_num )
    {
      const int64 raw = int64(int32(uint32(cy->numval()))) * int64(scale);
      *out_base = cx;
      *out_byte_off = cur->op == cot_sub ? -raw : raw;
      return true;
    }
    if ( cur->op == cot_add && cx != nullptr && cy != nullptr && cx->op == cot_num )
    {
      const int64 raw = int64(int32(uint32(cx->numval()))) * int64(scale);
      *out_base = cy;
      *out_byte_off = raw;
      return true;
    }
  }
  *out_base = cur;
  *out_byte_off = 0;
  return true;
}

int deref_access_size(const cexpr_t *ptr_expr)
{
  if ( ptr_expr == nullptr )
    return 0;
  if ( ptr_expr->op == cot_ptr && ptr_expr->ptrsize > 0 )
    return ptr_expr->ptrsize;
  if ( ptr_expr->op != cot_ptr && ptr_expr->op != cot_memptr )
    return 0;
  const size_t tsz = ptr_expr->type.get_size();
  return (tsz == 0 || tsz == BADSIZE) ? 0 : int(tsz);
}

bool decompose_mem_access(const cexpr_t *expr, const cexpr_t **out_base, int64 *out_byte_off,
                          int *out_size = nullptr)
{
  const cexpr_t *e = strip_casts(expr);
  if ( e == nullptr || out_base == nullptr || out_byte_off == nullptr )
    return false;
  if ( e->op == cot_ptr && e->x != nullptr )
  {
    if ( !decompose_addr_offset(e->x, out_base, out_byte_off) )
      return false;
    if ( out_size != nullptr )
      *out_size = deref_access_size(e);
    return true;
  }
  if ( e->op == cot_memptr && e->x != nullptr )
  {
    const cexpr_t *base = nullptr;
    int64 base_off = 0;
    if ( !decompose_addr_offset(e->x, &base, &base_off) )
      return false;
    *out_base = base;
    *out_byte_off = base_off + int64(e->m);
    if ( out_size != nullptr )
      *out_size = deref_access_size(e);
    return true;
  }
  return false;
}

bool is_stk_lvar(const cfunc_t *cfunc, int idx, int64 *out_stkoff = nullptr,
                 int *out_width = nullptr)
{
  if ( cfunc == nullptr || idx < 0 )
    return false;
  const lvars_t *lvars = const_cast<cfunc_t *>(cfunc)->get_lvars();
  if ( lvars == nullptr || size_t(idx) >= lvars->size() )
    return false;
  const lvar_t &lv = (*lvars)[size_t(idx)];
  if ( !lv.is_stk_var() )
    return false;
  if ( out_stkoff != nullptr )
    *out_stkoff = int64(lv.get_stkoff());
  if ( out_width != nullptr )
    *out_width = lv.width;
  return true;
}

cexpr_t *make_var_ref_expr(const cexpr_t &var_expr, ea_t ea)
{
  cexpr_t *inner = new cexpr_t(*strip_casts(&var_expr));
  if ( inner->type.empty() )
    inner->type = tinfo_t(BT_INT32);
  tinfo_t ptr_type;
  if ( !ptr_type.create_ptr(inner->type) )
  {
    delete inner;
    return nullptr;
  }
  cexpr_t *ref = new cexpr_t(cot_ref, inner);
  ref->ea = ea != BADADDR ? ea : var_expr.ea;
  ref->type = ptr_type;
  return ref;
}

cexpr_t *make_void_helper_call(const char *helper_name, const std::vector<cexpr_t *> &raw_args,
                               ea_t ea)
{
  carglist_t *list = new carglist_t;
  for ( cexpr_t *raw : raw_args )
  {
    if ( raw == nullptr )
      continue;
    if ( raw->type.empty() )
      raw->type = tinfo_t(BT_INT32);
    carg_t arg;
    arg.consume_cexpr(raw);
    list->push_back(arg);
  }
  cexpr_t *call = call_helper(tinfo_t(BT_VOID), list, "%s", helper_name);
  if ( call != nullptr )
    call->ea = ea;
  return call;
}

ea_t find_valid_ea(const cinsn_t &insn, ea_t fallback)
{
  if ( insn.ea != BADADDR )
    return insn.ea;
  struct ea_finder_t final : public ctree_visitor_t
  {
    ea_t found = BADADDR;
    ea_finder_t() : ctree_visitor_t(CV_FAST) {}
    int idaapi visit_insn(cinsn_t *i) override
    {
      if ( found == BADADDR && i->ea != BADADDR )
        found = i->ea;
      return 0;
    }
    int idaapi visit_expr(cexpr_t *e) override
    {
      if ( found == BADADDR && e->ea != BADADDR )
        found = e->ea;
      return 0;
    }
  };
  ea_finder_t finder;
  finder.apply_to(const_cast<cinsn_t *>(&insn), nullptr);
  return finder.found != BADADDR ? finder.found : fallback;
}

void replace_insn_with_call(cinsn_t *target, cexpr_t *call, ea_t fallback_ea)
{
  const ea_t ea = find_valid_ea(*target, fallback_ea);
  const int saved_label = target->label_num;
  target->cleanup();
  target->op = cit_expr;
  target->cexpr = call;
  target->ea = ea;
  target->label_num = saved_label;
  if ( target->cexpr->ea == BADADDR )
    target->cexpr->ea = ea;
}

// Collect label references (`goto L`) and label definitions across a ctree
// subtree or whole function.
struct label_and_use_collector_t final : public ctree_visitor_t
{
  std::map<int, size_t> goto_targets;
  std::set<int> defined_labels;
  std::map<int, size_t> var_reads;
  std::map<int, size_t> var_writes;

  label_and_use_collector_t() : ctree_visitor_t(CV_PARENTS) {}

  int idaapi visit_insn(cinsn_t *i) override
  {
    if ( i->label_num >= 0 )
      defined_labels.insert(i->label_num);
    if ( i->op == cit_goto && i->cgoto != nullptr )
      ++goto_targets[i->cgoto->label_num];
    return 0;
  }

  int idaapi visit_expr(cexpr_t *e) override
  {
    if ( e->op == cot_var )
    {
      const int idx = int(e->v.idx);
      bool is_pure_write = false;
      if ( !parents.empty() && parents.back()->is_expr() )
      {
        const cexpr_t *parent = static_cast<const cexpr_t *>(parents.back());
        if ( parent->op == cot_asg && strip_casts(parent->x) == e )
          is_pure_write = true;
        else if ( is_assignment(parent->op) && strip_casts(parent->x) == e )
          ++var_writes[idx];
      }
      if ( is_pure_write )
        ++var_writes[idx];
      else
        ++var_reads[idx];
    }
    return 0;
  }
};

void clear_labels_recursive(cinsn_t *insn)
{
  if ( insn == nullptr )
    return;
  struct label_clearer_t final : public ctree_visitor_t
  {
    label_clearer_t() : ctree_visitor_t(CV_FAST) {}
    int idaapi visit_insn(cinsn_t *i) override
    {
      i->label_num = -1;
      return 0;
    }
  };
  label_clearer_t clearer;
  clearer.apply_to(insn, nullptr);
}

// Match an abstract slot location: either a stack lvar at `stkoff` or a
// pointer deref `*(base_lvar + off)`.
struct slot_loc_t
{
  bool valid = false;
  bool is_stk = false;
  int lvar = -1;
  int64 offset = 0;
  const cexpr_t *raw_expr = nullptr;
};

slot_loc_t extract_slot_loc(const cfunc_t *cfunc, const cexpr_t *expr)
{
  slot_loc_t loc;
  const cexpr_t *e = strip_casts(expr);
  if ( e == nullptr )
    return loc;
  if ( e->op == cot_var )
  {
    int64 stkoff = 0;
    if ( is_stk_lvar(cfunc, int(e->v.idx), &stkoff) )
    {
      loc.valid = true;
      loc.is_stk = true;
      loc.lvar = int(e->v.idx);
      loc.offset = stkoff;
      loc.raw_expr = e;
    }
    return loc;
  }
  const cexpr_t *base = nullptr;
  int64 off = 0;
  if ( decompose_mem_access(e, &base, &off) && base != nullptr && base->op == cot_var )
  {
    loc.valid = true;
    loc.is_stk = false;
    loc.lvar = int(base->v.idx);
    loc.offset = off;
    loc.raw_expr = e;
  }
  return loc;
}

bool same_slot(const slot_loc_t &a, const slot_loc_t &b)
{
  if ( !a.valid || !b.valid || a.is_stk != b.is_stk )
    return false;
  if ( a.is_stk )
    return a.offset == b.offset;
  return a.lvar == b.lvar && a.offset == b.offset;
}

bool slot_delta_bytes(const slot_loc_t &from, const slot_loc_t &to, int64 *out_delta)
{
  if ( !from.valid || !to.valid || from.is_stk != to.is_stk )
    return false;
  if ( !from.is_stk && from.lvar != to.lvar )
    return false;
  *out_delta = to.offset - from.offset;
  return true;
}

cexpr_t *make_slot_address_expr(const slot_loc_t &slot, ea_t ea)
{
  if ( !slot.valid || slot.raw_expr == nullptr )
    return nullptr;
  if ( slot.is_stk || slot.raw_expr->op == cot_memptr )
    return make_var_ref_expr(*slot.raw_expr, ea);
  if ( slot.raw_expr->op == cot_ptr && slot.raw_expr->x != nullptr )
    return new cexpr_t(*strip_casts(slot.raw_expr->x));
  return nullptr;
}

bool is_var_or_slot(const cfunc_t *cfunc, const cexpr_t *e, int expected_var,
                    const slot_loc_t &expected_slot)
{
  e = strip_casts(e);
  if ( e == nullptr )
    return false;
  if ( expected_var >= 0 && e->op == cot_var && int(e->v.idx) == expected_var )
    return true;
  const slot_loc_t s = extract_slot_loc(cfunc, e);
  return same_slot(s, expected_slot);
}

bool extract_nonnull_cond(const cexpr_t &cond, const cexpr_t **out_tested)
{
  const cexpr_t *c = strip_casts(&cond);
  if ( c == nullptr )
    return false;
  if ( c->op == cot_ne && c->x != nullptr && c->y != nullptr )
  {
    const cexpr_t *cy = strip_casts(c->y);
    const cexpr_t *cx = strip_casts(c->x);
    if ( cy != nullptr && cy->op == cot_num && cy->numval() == 0 )
    {
      *out_tested = cx;
      return true;
    }
    if ( cx != nullptr && cx->op == cot_num && cx->numval() == 0 )
    {
      *out_tested = cy;
      return true;
    }
  }
  if ( c->op == cot_var )
  {
    *out_tested = c;
    return true;
  }
  return false;
}

// Extract vtable call info from `__indirect_function_table[idx](args...)`.
bool extract_indirect_vtable_call(const cexpr_t *expr, uint64 *out_vtoff, int *out_vtable_obj_lvar,
                                  int *out_call_arg_lvar, size_t *out_arg_count,
                                  const cexpr_t **out_arg0_expr)
{
  const cexpr_t *call = strip_casts(expr);
  if ( call == nullptr || call->op != cot_call || call->x == nullptr || call->a == nullptr )
    return false;
  const cexpr_t *callee = strip_casts(call->x);
  if ( !is_indirect_table_expr(callee) )
    return false;
  *out_arg_count = call->a->size();
  if ( !call->a->empty() )
  {
    *out_arg0_expr = &(*call->a)[0];
    const cexpr_t *arg0 = strip_casts(&(*call->a)[0]);
    if ( arg0 != nullptr && arg0->op == cot_var )
      *out_call_arg_lvar = int(arg0->v.idx);
  }
  const cexpr_t *outer = strip_casts(callee->y);
  if ( outer == nullptr || outer->op != cot_ptr || outer->x == nullptr )
    return false;
  const cexpr_t *inner_base = nullptr;
  int64 byte_off = 0;
  if ( !decompose_addr_offset(outer->x, &inner_base, &byte_off) || inner_base == nullptr
       || inner_base->op != cot_ptr || inner_base->x == nullptr || byte_off < 0 )
    return false;
  const cexpr_t *obj = strip_casts(inner_base->x);
  if ( obj == nullptr || obj->op != cot_var )
    return false;
  *out_vtoff = uint64(byte_off);
  *out_vtable_obj_lvar = int(obj->v.idx);
  return true;
}

class wasmhex_libcxx_rewriter_t final : public ctree_visitor_t
{
  cfunc_t *cfunc;
  label_and_use_collector_t func_info;
  std::set<cinsn_t *> to_remove;
  libcxx_detail::libcxx_match_stats_t stats;

  // SSO dtor marks (blob 'b' v1) — the loader proved the tag
  // guard, the free call and the string-struct offsets on the instruction
  // stream; the consumer keeps only the ctree-side checks.
  loader::libcxx_function_ir_t libcxx_ir;
  bool libcxx_ir_loaded = false;
  std::map<ea_t, const loader::libcxx_sso_dtor_ir_t *> sso_mark_by_guard_ea;
  std::map<ea_t, const loader::libcxx_sso_dtor_ir_t *> sso_mark_by_tag_ea;

  void load_libcxx_marks()
  {
    if ( libcxx_ir_loaded )
      return;
    libcxx_ir_loaded = true;
    netnode wasm_node(wasmhex_node_name, 0, false);
    if ( wasm_node == BADNODE )
      return;
    bytevec_t blob;
    if ( wasm_node.getblob_ea(&blob, cfunc->entry_ea, 'b') <= 0 || blob.empty() )
      return;
    const std::size_t words = blob.size() / sizeof(std::uint32_t);
    std::vector<std::uint32_t> wire(words);
    std::memcpy(wire.data(), blob.begin(), words * sizeof(std::uint32_t));
    if ( !loader::decode_libcxx_function_ir_blob(wire.data(), wire.size(), &libcxx_ir) )
    {
      libcxx_ir = loader::libcxx_function_ir_t{};
      return;
    }
    for ( const loader::libcxx_sso_dtor_ir_t &site : libcxx_ir.sso_dtors )
    {
      sso_mark_by_guard_ea.emplace(cfunc->entry_ea + site.guard_offset, &site);
      sso_mark_by_tag_ea.emplace(cfunc->entry_ea + site.tag_load_offset, &site);
    }
  }

  const loader::libcxx_sso_dtor_ir_t *sso_mark_for(const cinsn_t *insn) const
  {
    if ( insn == nullptr || insn->op != cit_if || insn->cif == nullptr )
      return nullptr;
    // The ctree `if` EA convention differs from the wasm `if` opcode offset
    // across Hex-Rays versions, so the tag-load EA (an expression inside the
    // condition — always present, always the same instruction) is the stable
    // key; the statement EA is tried first as the cheaper path.
    if ( insn->ea != BADADDR )
    {
      const auto it = sso_mark_by_guard_ea.find(insn->ea);
      if ( it != sso_mark_by_guard_ea.end() )
        return it->second;
    }
    struct tag_ea_finder_t final : ctree_visitor_t
    {
      const std::map<ea_t, const loader::libcxx_sso_dtor_ir_t *> *by_guard;
      const loader::libcxx_sso_dtor_ir_t *hit = nullptr;
      tag_ea_finder_t(const std::map<ea_t, const loader::libcxx_sso_dtor_ir_t *> *m)
          : ctree_visitor_t(CV_FAST), by_guard(m)
      {
      }
      int idaapi visit_expr(cexpr_t *e) override
      {
        if ( hit == nullptr && e != nullptr && e->ea != BADADDR )
        {
          const auto it = by_guard->find(e->ea);
          if ( it != by_guard->end() )
            hit = it->second;
        }
        return hit != nullptr ? 1 : 0;
      }
    };
    // index the marks by tag-load EA as well (built once at load)
    tag_ea_finder_t finder(&sso_mark_by_tag_ea);
    finder.apply_to(&const_cast<cinsn_t *>(insn)->cif->expr, nullptr);
    return finder.hit;
  }

  // The free call identity, mark-first: the mark carries the proven free
  // call offset; the name check stays for unmarked sites.
  bool is_free_call_stmt_marked(const cinsn_t &inner, const cexpr_t **out_arg,
                                const loader::libcxx_sso_dtor_ir_t *mark)
  {
    if ( mark == nullptr )
      return is_free_call_stmt(inner, out_arg);
    if ( inner.op != cit_expr || inner.cexpr == nullptr )
      return false;
    const cexpr_t *call = strip_casts(inner.cexpr);
    if ( call == nullptr || call->op != cot_call || call->ea == BADADDR )
      return false;
    if ( call->ea != cfunc->entry_ea + mark->free_call_offset )
      return false;
    if ( call->a == nullptr || call->a->size() != 1 )
      return false;
    if ( out_arg != nullptr )
      *out_arg = &(*call->a)[0];
    return true;
  }

  bool has_external_goto_into(const std::vector<cinsn_t *> &region_stmts,
                              bool allow_entry_label_on_first) const
  {
    label_and_use_collector_t region_info;
    for ( cinsn_t *s : region_stmts )
      if ( s != nullptr )
        region_info.apply_to(s, nullptr);
    const int entry_label
      = (allow_entry_label_on_first && !region_stmts.empty() && region_stmts[0] != nullptr)
      ? region_stmts[0]->label_num
      : -1;
    for ( int lbl : region_info.defined_labels )
    {
      if ( lbl == entry_label )
        continue;
      const auto f_it = func_info.goto_targets.find(lbl);
      const size_t total_gotos = f_it == func_info.goto_targets.end() ? 0 : f_it->second;
      const auto r_it = region_info.goto_targets.find(lbl);
      const size_t internal_gotos = r_it == region_info.goto_targets.end() ? 0 : r_it->second;
      if ( total_gotos > internal_gotos )
        return true;
    }
    return false;
  }

  // Check that every temporary local written AND read inside `region_stmts`
  // has 0 reads outside `region_stmts`.
  bool region_temps_do_not_escape(const std::vector<cinsn_t *> &region_stmts,
                                  const std::set<int> &require_all_uses_vanish) const
  {
    label_and_use_collector_t region_info;
    for ( cinsn_t *s : region_stmts )
      if ( s != nullptr )
        region_info.apply_to(s, nullptr);
    for ( int v : require_all_uses_vanish )
    {
      if ( v < 0 )
        return false;
      const auto f_r = func_info.var_reads.find(v);
      const size_t total_r = f_r == func_info.var_reads.end() ? 0 : f_r->second;
      const auto r_r = region_info.var_reads.find(v);
      const size_t in_r = r_r == region_info.var_reads.end() ? 0 : r_r->second;
      const auto f_w = func_info.var_writes.find(v);
      const size_t total_w = f_w == func_info.var_writes.end() ? 0 : f_w->second;
      const auto r_w = region_info.var_writes.find(v);
      const size_t in_w = r_w == region_info.var_writes.end() ? 0 : r_w->second;
      if ( total_r > in_r || total_w > in_w )
        return false;
    }
    for ( const auto &kv : region_info.var_writes )
    {
      const int v = kv.first;
      if ( is_stk_lvar(cfunc, v) )
        continue;
      const auto r_r = region_info.var_reads.find(v);
      if ( r_r == region_info.var_reads.end() || r_r->second == 0 )
        continue;
      const auto f_r = func_info.var_reads.find(v);
      const size_t total_r = f_r == func_info.var_reads.end() ? 0 : f_r->second;
      if ( total_r > r_r->second )
        return false;
    }
    return true;
  }

  // Pattern 3: std::vector<std::string>::~vector(v)
  bool try_match_vector_string_dtor(cblock_t *block, cblock_t::iterator it)
  {
    if ( it->op != cit_if || it->cif == nullptr || it->cif->ielse != nullptr
         || it->cif->ithen == nullptr || it->cif->ithen->op != cit_block
         || it->cif->ithen->cblock == nullptr )
      return false;

    cblock_t *outer_then = it->cif->ithen->cblock;
    if ( outer_then->size() != 5 )
      return false;

    auto s0 = outer_then->begin();
    auto s1 = next_insn_it(s0);
    auto s2 = next_insn_it(s1);
    auto s3 = next_insn_it(s2);
    auto s4 = next_insn_it(s3);
    if ( s2->op != cit_if || s2->cif == nullptr || s2->cif->ielse != nullptr
         || s2->cif->ithen == nullptr || s2->cif->ithen->op != cit_block
         || s2->cif->ithen->cblock == nullptr || s2->cif->ithen->cblock->empty() )
      return false;
    cblock_t *inner_then = s2->cif->ithen->cblock;
    if ( inner_then->front().op != cit_do || inner_then->front().cdo == nullptr )
      return false;
    if ( !is_free_call_stmt(*s4) )
      return false;

    // Look for optional `v_beg = <vec_begin>;` immediately before `it`.
    cinsn_t *prefix_stmt = nullptr;
    int v_beg_copy = -1;
    slot_loc_t begin_slot;
    if ( it != block->begin() )
    {
      auto prev_it = prev_insn_it(it);
      if ( to_remove.count(&*prev_it) == 0 && prev_it->op == cit_expr && prev_it->cexpr != nullptr
           && prev_it->cexpr->op == cot_asg && prev_it->cexpr->x != nullptr )
      {
        const cexpr_t *lhs = strip_casts(prev_it->cexpr->x);
        const slot_loc_t rhs_slot = extract_slot_loc(cfunc, prev_it->cexpr->y);
        if ( lhs != nullptr && lhs->op == cot_var && !is_stk_lvar(cfunc, int(lhs->v.idx))
             && rhs_slot.valid )
        {
          prefix_stmt = &*prev_it;
          v_beg_copy = int(lhs->v.idx);
          begin_slot = rhs_slot;
        }
      }
    }

    libcxx_detail::vector_string_dtor_candidate_t cand;

    const cexpr_t *outer_tested = nullptr;
    if ( extract_nonnull_cond(it->cif->expr, &outer_tested) && outer_tested != nullptr )
    {
      if ( !begin_slot.valid )
        begin_slot = extract_slot_loc(cfunc, outer_tested);
      if ( is_var_or_slot(cfunc, outer_tested, v_beg_copy, begin_slot) )
        cand.has_outer_nonnull_check = true;
    }

    int v_free_ptr = -1;
    if ( s0->op == cit_expr && s0->cexpr != nullptr && s0->cexpr->op == cot_asg
         && s0->cexpr->x != nullptr )
    {
      const cexpr_t *lhs = strip_casts(s0->cexpr->x);
      if ( lhs != nullptr && lhs->op == cot_var
           && is_var_or_slot(cfunc, s0->cexpr->y, v_beg_copy, begin_slot) )
      {
        v_free_ptr = int(lhs->v.idx);
      }
    }

    int v_it = -1;
    slot_loc_t end_slot;
    if ( s1->op == cit_expr && s1->cexpr != nullptr && s1->cexpr->op == cot_asg
         && s1->cexpr->x != nullptr )
    {
      const cexpr_t *lhs = strip_casts(s1->cexpr->x);
      end_slot = extract_slot_loc(cfunc, s1->cexpr->y);
      if ( lhs != nullptr && lhs->op == cot_var && end_slot.valid )
        v_it = int(lhs->v.idx);
    }

    int64 end_minus_begin = 0;
    if ( slot_delta_bytes(begin_slot, end_slot, &end_minus_begin) )
      cand.end_minus_begin_bytes = end_minus_begin;

    const cexpr_t *ne_cond = strip_casts(&s2->cif->expr);
    if ( ne_cond != nullptr && ne_cond->op == cot_ne && ne_cond->x != nullptr
         && ne_cond->y != nullptr )
    {
      const bool ab = is_var_or_slot(cfunc, ne_cond->x, v_beg_copy, begin_slot)
        && is_var_or_slot(cfunc, ne_cond->y, v_it, end_slot);
      const bool ba = is_var_or_slot(cfunc, ne_cond->y, v_beg_copy, begin_slot)
        && is_var_or_slot(cfunc, ne_cond->x, v_it, end_slot);
      cand.has_begin_ne_end_check = ab || ba;
    }

    int v_prev = -1;
    cdo_t *cdo = inner_then->front().cdo;
    if ( cdo != nullptr && cdo->body != nullptr && cdo->body->op == cit_block
         && cdo->body->cblock != nullptr && cdo->body->cblock->size() == 3 )
    {
      cand.has_do_loop = true;
      auto d0 = cdo->body->cblock->begin();
      auto d1 = next_insn_it(d0);
      auto d2 = next_insn_it(d1);

      if ( d0->op == cit_expr && d0->cexpr != nullptr && d0->cexpr->op == cot_asg
           && d0->cexpr->x != nullptr )
      {
        const cexpr_t *lhs = strip_casts(d0->cexpr->x);
        const cexpr_t *base = nullptr;
        int64 off = 0;
        if ( lhs != nullptr && lhs->op == cot_var
             && decompose_addr_offset(d0->cexpr->y, &base, &off) && base != nullptr
             && base->op == cot_var && int(base->v.idx) == v_it )
        {
          v_prev = int(lhs->v.idx);
          cand.step_prev_bytes = off;
        }
      }

      if ( d1->op == cit_if && d1->cif != nullptr && d1->cif->ielse == nullptr
           && d1->cif->ithen != nullptr && d1->cif->ithen->op == cit_block
           && d1->cif->ithen->cblock != nullptr && d1->cif->ithen->cblock->size() == 1 )
      {
        const cexpr_t *icond = strip_casts(&d1->cif->expr);
        if ( icond != nullptr && icond->op == cot_slt && icond->x != nullptr && icond->y != nullptr
             && icond->y->op == cot_num && icond->y->numval() == 0 )
        {
          cand.tag_is_signed_lt_zero = true;
          const cexpr_t *tag_deref = strip_casts(icond->x);
          if ( tag_deref != nullptr && tag_deref->op == cot_ptr && tag_deref->x != nullptr )
          {
            cand.tag_read_size = deref_access_size(tag_deref);
            const cexpr_t *tag_base = nullptr;
            int64 tag_off = 0;
            if ( decompose_addr_offset(tag_deref->x, &tag_base, &tag_off) && tag_base != nullptr
                 && tag_base->op == cot_var && int(tag_base->v.idx) == v_it )
            {
              cand.tag_offset_from_it = tag_off;
            }
          }
        }
        const cexpr_t *free_arg = nullptr;
        if ( is_free_call_stmt(d1->cif->ithen->cblock->front(), &free_arg) && free_arg != nullptr )
        {
          const cexpr_t *fa = strip_casts(free_arg);
          if ( fa != nullptr && fa->op == cot_ptr && fa->x != nullptr )
          {
            const cexpr_t *fa_base = strip_casts(fa->x);
            if ( fa_base != nullptr && fa_base->op == cot_var && int(fa_base->v.idx) == v_prev )
              cand.calls_free_on_prev_deref = true;
          }
        }
      }

      if ( d2->op == cit_expr && d2->cexpr != nullptr )
      {
        const cexpr_t *e2 = d2->cexpr;
        if ( e2->op == cot_asgsub && e2->x != nullptr && e2->y != nullptr )
        {
          const cexpr_t *lhs = strip_casts(e2->x);
          const cexpr_t *rhs = strip_casts(e2->y);
          if ( lhs != nullptr && lhs->op == cot_var && int(lhs->v.idx) == v_it && rhs != nullptr
               && rhs->op == cot_num )
          {
            const size_t scale = pointed_elem_size(e2->x->type);
            cand.iter_decrement_bytes = int64(rhs->numval()) * int64(scale);
          }
        }
      }

      const cexpr_t *wcond = strip_casts(&cdo->expr);
      if ( wcond != nullptr && wcond->op == cot_ne && wcond->x != nullptr && wcond->y != nullptr )
      {
        const cexpr_t *wx = strip_casts(wcond->x);
        const cexpr_t *wy = strip_casts(wcond->y);
        const bool ab = wx != nullptr && wx->op == cot_var && int(wx->v.idx) == v_prev
          && is_var_or_slot(cfunc, wy, v_beg_copy, begin_slot);
        const bool ba = wy != nullptr && wy->op == cot_var && int(wy->v.idx) == v_prev
          && is_var_or_slot(cfunc, wx, v_beg_copy, begin_slot);
        cand.loop_cond_compares_prev_to_begin = ab || ba;
      }
    }

    if ( s3->op == cit_expr && s3->cexpr != nullptr && s3->cexpr->op == cot_asg )
    {
      const slot_loc_t lhs_slot = extract_slot_loc(cfunc, s3->cexpr->x);
      if ( same_slot(lhs_slot, end_slot)
           && is_var_or_slot(cfunc, s3->cexpr->y, v_beg_copy, begin_slot) )
      {
        cand.resets_end_to_begin = true;
      }
    }

    const cexpr_t *final_free_arg = nullptr;
    if ( is_free_call_stmt(*s4, &final_free_arg) && final_free_arg != nullptr )
    {
      if ( is_var_or_slot(cfunc, final_free_arg, v_free_ptr, begin_slot)
           || is_var_or_slot(cfunc, final_free_arg, v_beg_copy, begin_slot) )
      {
        cand.calls_free_on_begin = true;
      }
    }

    std::vector<cinsn_t *> region;
    if ( prefix_stmt != nullptr )
      region.push_back(prefix_stmt);
    region.push_back(&*it);
    cand.has_external_incoming_label = has_external_goto_into(region, true);

    std::set<int> temps;
    if ( v_beg_copy >= 0 )
      temps.insert(v_beg_copy);
    if ( v_free_ptr >= 0 )
      temps.insert(v_free_ptr);
    if ( v_it >= 0 )
      temps.insert(v_it);
    if ( v_prev >= 0 )
      temps.insert(v_prev);
    cand.temps_do_not_escape = region_temps_do_not_escape(region, temps);

    if ( !libcxx_detail::verify_vector_string_dtor_site(cand) )
    {
      ++stats.skipped[2];
      return false;
    }

    const ea_t ea = find_valid_ea(*it, cfunc->entry_ea);
    cexpr_t *vec_addr = make_slot_address_expr(begin_slot, ea);
    if ( vec_addr == nullptr )
    {
      ++stats.skipped[2];
      return false;
    }
    cexpr_t *call = make_void_helper_call("std::vector<std::string>::~vector", { vec_addr }, ea);
    if ( call == nullptr )
    {
      delete vec_addr;
      ++stats.skipped[2];
      return false;
    }

    if ( prefix_stmt != nullptr )
    {
      if ( prefix_stmt->label_num >= 0 && it->label_num < 0 )
      {
        it->label_num = prefix_stmt->label_num;
        prefix_stmt->label_num = -1;
      }
      to_remove.insert(prefix_stmt);
    }
    clear_labels_recursive(it->cif->ithen);
    replace_insn_with_call(&*it, call, cfunc->entry_ea);
    ++stats.matched[2];
    return true;
  }

  // Pattern 1: unique_ptr virtual delete (vtable+4)
  bool try_match_unique_ptr_delete(cinsn_t *insn)
  {
    if ( insn->op == cit_expr && insn->cexpr != nullptr )
    {
      uint64 vtoff = 0;
      int vobj = -1;
      int carg = -1;
      size_t argc = 0;
      const cexpr_t *arg0_expr = nullptr;
      if ( extract_indirect_vtable_call(insn->cexpr, &vtoff, &vobj, &carg, &argc, &arg0_expr) )
      {
        // Unconditional indirect vtable call without `if (p != 0)` guard:
        // record as a skipped candidate under the strict unique_ptr rule.
        ++stats.skipped[0];
      }
      return false;
    }

    if ( insn->op != cit_if || insn->cif == nullptr || insn->cif->ithen == nullptr
         || insn->cif->ithen->op != cit_block || insn->cif->ithen->cblock == nullptr
         || insn->cif->ithen->cblock->size() != 1 )
      return false;

    cinsn_t &inner = insn->cif->ithen->cblock->front();
    if ( inner.op != cit_expr || inner.cexpr == nullptr )
      return false;

    uint64 vtoff = 0;
    int vobj = -1;
    int carg = -1;
    size_t argc = 0;
    const cexpr_t *arg0_expr = nullptr;
    if ( !extract_indirect_vtable_call(inner.cexpr, &vtoff, &vobj, &carg, &argc, &arg0_expr) )
      return false;

    libcxx_detail::unique_ptr_delete_candidate_t cand;
    cand.has_else = insn->cif->ielse != nullptr;
    cand.is_indirect_table_call = true;
    cand.index_reads_vtable_from_ptr = vobj >= 0;
    cand.vtable_byte_offset = vtoff;
    cand.arg_count = argc;
    cand.vtable_obj_lvar = vobj;
    cand.call_arg_lvar = carg;

    const cexpr_t *tested = nullptr;
    if ( extract_nonnull_cond(insn->cif->expr, &tested) && tested != nullptr
         && tested->op == cot_var )
    {
      cand.has_null_guard = true;
      cand.cond_lvar = int(tested->v.idx);
    }

    std::vector<cinsn_t *> region = { insn };
    cand.inner_stmt_has_external_label
      = inner.label_num >= 0 || has_external_goto_into(region, true);

    if ( !libcxx_detail::verify_unique_ptr_delete_site(cand) || arg0_expr == nullptr )
    {
      ++stats.skipped[0];
      return false;
    }

    const ea_t ea = find_valid_ea(inner, find_valid_ea(*insn, cfunc->entry_ea));
    cexpr_t *arg_clone = new cexpr_t(*strip_casts(arg0_expr));
    cexpr_t *call = make_void_helper_call("delete", { arg_clone }, ea);
    if ( call == nullptr )
    {
      delete arg_clone;
      ++stats.skipped[0];
      return false;
    }

    replace_insn_with_call(insn, call, cfunc->entry_ea);
    if ( insn->ea != BADADDR )
    {
      treeloc_t loc;
      loc.ea = insn->ea;
      loc.itp = ITP_SEMI;
      cfunc->set_user_cmt(loc, "std::default_delete");
    }
    ++stats.matched[0];
    return true;
  }

  // Pattern 2: std::string SSO destructor
  bool try_match_sso_string_dtor(cinsn_t *insn)
  {
    if ( insn->op != cit_if || insn->cif == nullptr || insn->cif->ithen == nullptr
         || insn->cif->ithen->op != cit_block || insn->cif->ithen->cblock == nullptr
         || insn->cif->ithen->cblock->size() != 1 )
      return false;

    const loader::libcxx_sso_dtor_ir_t *mark = sso_mark_for(insn);
    if ( mark != nullptr )
      ++stats.sso_mark_hits;

    cinsn_t &inner = insn->cif->ithen->cblock->front();
    const cexpr_t *free_arg = nullptr;
    if ( !is_free_call_stmt_marked(inner, &free_arg, mark) || free_arg == nullptr )
      return false;

    const cexpr_t *cond = strip_casts(&insn->cif->expr);
    if ( cond == nullptr || cond->op != cot_slt || cond->x == nullptr || cond->y == nullptr )
      return false;
    const cexpr_t *zero = strip_casts(cond->y);
    if ( zero == nullptr || zero->op != cot_num || zero->numval() != 0 )
      return false;

    libcxx_detail::sso_string_dtor_candidate_t cand;
    cand.has_else = insn->cif->ielse != nullptr;
    cand.inner_stmt_has_external_label
      = inner.label_num >= 0 || has_external_goto_into({ insn }, true);
    cand.is_signed_lt_zero = true;
    cand.is_free_call = true;
    cand.free_arg_count = 1;
    cand.mark_proven = mark != nullptr;

    const cexpr_t *tag_lhs = strip_casts(cond->x);
    const cexpr_t *data_rhs = strip_casts(free_arg);
    cexpr_t *string_addr_expr = nullptr;
    const ea_t ea = find_valid_ea(inner, find_valid_ea(*insn, cfunc->entry_ea));

    if ( tag_lhs != nullptr && tag_lhs->op == cot_var && data_rhs != nullptr
         && data_rhs->op == cot_var )
    {
      int64 tag_stkoff = 0;
      int64 data_stkoff = 0;
      int tag_w = 0;
      int data_w = 0;
      if ( is_stk_lvar(cfunc, int(tag_lhs->v.idx), &tag_stkoff, &tag_w)
           && is_stk_lvar(cfunc, int(data_rhs->v.idx), &data_stkoff, &data_w) )
      {
        cand.is_stack_form = true;
        cand.tag_read_size = tag_w;
        cand.data_read_size = data_w;
        cand.tag_stkoff = tag_stkoff;
        cand.data_stkoff = data_stkoff;
        if ( libcxx_detail::verify_sso_string_dtor_site(cand) )
          string_addr_expr = make_var_ref_expr(*data_rhs, ea);
      }
    }
    else if ( tag_lhs != nullptr && data_rhs != nullptr )
    {
      const cexpr_t *tag_base = nullptr;
      const cexpr_t *data_base = nullptr;
      int64 tag_off = 0;
      int64 data_off = 0;
      int tag_sz = 0;
      int data_sz = 0;
      if ( decompose_mem_access(tag_lhs, &tag_base, &tag_off, &tag_sz)
           && decompose_mem_access(data_rhs, &data_base, &data_off, &data_sz) )
      {
        cand.is_pointer_form = true;
        cand.tag_read_size = tag_sz;
        cand.data_read_size = data_sz;
        cand.tag_byte_offset = tag_off;
        cand.data_byte_offset = data_off;
        cand.same_base_expr = tag_base != nullptr && data_base != nullptr && tag_base->op == cot_var
          && data_base->op == cot_var && tag_base->v.idx == data_base->v.idx;
        if ( libcxx_detail::verify_sso_string_dtor_site(cand) )
          string_addr_expr = new cexpr_t(*data_base);
      }
    }

    if ( !libcxx_detail::verify_sso_string_dtor_site(cand) || string_addr_expr == nullptr )
    {
      delete string_addr_expr;
      ++stats.skipped[1];
      return false;
    }

    cexpr_t *call = make_void_helper_call("std::__1::string::~string", { string_addr_expr }, ea);
    if ( call == nullptr )
    {
      delete string_addr_expr;
      ++stats.skipped[1];
      return false;
    }

    replace_insn_with_call(insn, call, cfunc->entry_ea);
    ++stats.matched[1];
    return true;
  }

  // Pattern 4: std::vector<T>::push_back(&vec, val)
  struct push_back_scan_t final : public ctree_visitor_t
  {
    const cfunc_t *cfunc;
    const cexpr_t *throw_len_vec_arg = nullptr;
    uint64 max_size_const = 0;
    uint64 elem_size = 0;
    bool has_throw_length_error = false;
    bool has_bad_array_new_length = false;
    bool has_operator_new = false;
    bool has_backward_copy_loop = false;
    bool has_old_buffer_free = false;

    explicit push_back_scan_t(const cfunc_t *cf) : ctree_visitor_t(CV_FAST), cfunc(cf) {}

    int idaapi visit_insn(cinsn_t *i) override
    {
      if ( i->op == cit_do || i->op == cit_for )
        has_backward_copy_loop = true;
      if ( i->op == cit_if && i->cif != nullptr && i->cif->ielse == nullptr
           && i->cif->ithen != nullptr && i->cif->ithen->op == cit_block
           && i->cif->ithen->cblock != nullptr && i->cif->ithen->cblock->size() == 1 )
      {
        const cexpr_t *tested = nullptr;
        if ( extract_nonnull_cond(i->cif->expr, &tested)
             && is_free_call_stmt(i->cif->ithen->cblock->front()) )
        {
          has_old_buffer_free = true;
        }
      }
      if ( i->op == cit_if && i->cif != nullptr )
      {
        const cexpr_t *c = strip_casts(&i->cif->expr);
        if ( c != nullptr && (c->op == cot_uge || c->op == cot_sge) && c->y != nullptr )
        {
          const cexpr_t *cy = strip_casts(c->y);
          if ( cy != nullptr && cy->op == cot_num
               && (cy->numval() == 0x40000000ULL || cy->numval() == 0x20000000ULL
                   || cy->numval() == 0x10000000ULL || cy->numval() == 0x08000000ULL)
               && max_size_const == 0 )
          {
            max_size_const = uint64(cy->numval());
          }
        }
      }
      return 0;
    }

    int idaapi visit_expr(cexpr_t *e) override
    {
      if ( e->op != cot_call || e->x == nullptr )
        return 0;
      qstring name;
      if ( !callee_name(e, &name) )
        return 0;
      if ( name.find("__throw_length_error") != qstring::npos && e->a != nullptr && !e->a->empty() )
      {
        has_throw_length_error = true;
        throw_len_vec_arg = &(*e->a)[0];
      }
      else if ( name.find("cxa_throw_bad_array_new_length") != qstring::npos )
      {
        has_bad_array_new_length = true;
      }
      else if ( (name == "_Znwm" || name == "operator new") && e->a != nullptr
                && e->a->size() == 1 )
      {
        has_operator_new = true;
        const cexpr_t *sz_arg = strip_casts(&(*e->a)[0]);
        if ( sz_arg != nullptr && sz_arg->op == cot_mul && sz_arg->x != nullptr
             && sz_arg->y != nullptr )
        {
          const cexpr_t *mx = strip_casts(sz_arg->x);
          const cexpr_t *my = strip_casts(sz_arg->y);
          if ( mx != nullptr && mx->op == cot_num )
            elem_size = uint64(mx->numval());
          else if ( my != nullptr && my->op == cot_num )
            elem_size = uint64(my->numval());
        }
      }
      else if ( strncmp(name.c_str(), "invoke_", 7) == 0 && e->a != nullptr && e->a->size() >= 2 )
      {
        const cexpr_t *fn_arg = strip_wrappers(&(*e->a)[0]);
        if ( fn_arg != nullptr && fn_arg->op == cot_obj )
        {
          qstring got_name;
          get_name(&got_name, fn_arg->obj_ea);
          if ( got_name.find("_Znwm") != qstring::npos )
          {
            has_operator_new = true;
            const cexpr_t *sz_arg = strip_casts(&(*e->a)[1]);
            if ( sz_arg != nullptr && sz_arg->op == cot_mul && sz_arg->x != nullptr
                 && sz_arg->y != nullptr )
            {
              const cexpr_t *mx = strip_casts(sz_arg->x);
              const cexpr_t *my = strip_casts(sz_arg->y);
              if ( mx != nullptr && mx->op == cot_num )
                elem_size = uint64(mx->numval());
              else if ( my != nullptr && my->op == cot_num )
                elem_size = uint64(my->numval());
            }
          }
        }
      }
      return 0;
    }
  };

  bool try_match_vector_push_back(cblock_t *block, cblock_t::iterator it)
  {
    if ( it->op != cit_if || it->cif == nullptr || it->cif->ithen == nullptr
         || it->cif->ithen->op != cit_block || it->cif->ithen->cblock == nullptr )
      return false;

    const cexpr_t *cond = strip_casts(&it->cif->expr);
    if ( cond == nullptr || cond->x == nullptr || cond->y == nullptr )
      return false;

    bool is_form_a = false;
    bool is_form_b = false;
    cblock_t::iterator end_stmt_it = block->end();

    if ( it->cif->ielse != nullptr && (cond->op == cot_uge || cond->op == cot_sge) )
    {
      push_back_scan_t probe(cfunc);
      probe.apply_to(it->cif->ithen, nullptr);
      if ( !probe.has_throw_length_error )
        return false;
      auto next_it = next_insn_it(it);
      if ( next_it == block->end() || to_remove.count(&*next_it) != 0 )
      {
        ++stats.skipped[3];
        return false;
      }
      is_form_a = true;
      end_stmt_it = next_it;
    }
    else if ( it->cif->ielse == nullptr && (cond->op == cot_ult || cond->op == cot_slt) )
    {
      cblock_t *then_blk = it->cif->ithen->cblock;
      if ( then_blk->empty() || then_blk->back().op != cit_goto
           || then_blk->back().cgoto == nullptr )
        return false;
      const int target_lbl = then_blk->back().cgoto->label_num;
      bool saw_throw_len = false;
      for ( auto scan_it = next_insn_it(it); scan_it != block->end(); ++scan_it )
      {
        if ( scan_it->label_num == target_lbl )
        {
          end_stmt_it = scan_it;
          break;
        }
        push_back_scan_t probe(cfunc);
        probe.apply_to(&*scan_it, nullptr);
        if ( probe.has_throw_length_error )
          saw_throw_len = true;
      }
      if ( !saw_throw_len || end_stmt_it == block->end() )
        return false;
      is_form_b = true;
    }
    else
    {
      return false;
    }

    // Walk backward up to 3 preceding statements in `block` to capture
    // `v_val = <val>; v_end = <vec_end>; [v_cap = <vec_cap>;]`.
    cblock_t::iterator start_stmt_it = it;
    const cexpr_t *val_expr = nullptr;
    slot_loc_t pre_end_slot;
    slot_loc_t pre_cap_slot;
    int v_end_temp = -1;
    int v_cap_temp = -1;
    int v_val_temp = -1;

    for ( int step = 0; step < 3 && start_stmt_it != block->begin(); ++step )
    {
      auto prev_it = prev_insn_it(start_stmt_it);
      if ( to_remove.count(&*prev_it) != 0 || prev_it->op != cit_expr || prev_it->cexpr == nullptr
           || prev_it->cexpr->op != cot_asg || prev_it->cexpr->x == nullptr
           || prev_it->cexpr->y == nullptr )
        break;
      const cexpr_t *lhs = strip_casts(prev_it->cexpr->x);
      if ( lhs == nullptr || lhs->op != cot_var || is_stk_lvar(cfunc, int(lhs->v.idx)) )
        break;
      const int lhs_var = int(lhs->v.idx);
      const slot_loc_t rhs_slot = extract_slot_loc(cfunc, prev_it->cexpr->y);
      const cexpr_t *cx = strip_casts(cond->x);
      const cexpr_t *cy = strip_casts(cond->y);
      const slot_loc_t cx_slot = extract_slot_loc(cfunc, cx);
      const slot_loc_t cy_slot = extract_slot_loc(cfunc, cy);
      if ( step == 0 && rhs_slot.valid && !pre_end_slot.valid )
      {
        // Either `v_cap = vec.cap` (when 3 prefix stmts) or `v_end = vec.end` (when 2).
        if ( (cy != nullptr && cy->op == cot_var && int(cy->v.idx) == lhs_var)
             || same_slot(cy_slot, rhs_slot) )
        {
          v_cap_temp = lhs_var;
          pre_cap_slot = rhs_slot;
          start_stmt_it = prev_it;
          continue;
        }
        if ( (cx != nullptr && cx->op == cot_var && int(cx->v.idx) == lhs_var)
             || same_slot(cx_slot, rhs_slot) )
        {
          v_end_temp = lhs_var;
          pre_end_slot = rhs_slot;
          start_stmt_it = prev_it;
          continue;
        }
      }
      if ( rhs_slot.valid && !pre_end_slot.valid )
      {
        if ( (cx != nullptr && cx->op == cot_var && int(cx->v.idx) == lhs_var)
             || same_slot(cx_slot, rhs_slot) )
        {
          v_end_temp = lhs_var;
          pre_end_slot = rhs_slot;
          start_stmt_it = prev_it;
          continue;
        }
      }
      if ( pre_end_slot.valid && val_expr == nullptr )
      {
        v_val_temp = lhs_var;
        val_expr = prev_it->cexpr->y;
        start_stmt_it = prev_it;
        break;
      }
      break;
    }

    std::vector<cinsn_t *> region;
    for ( auto cur = start_stmt_it;; ++cur )
    {
      region.push_back(&*cur);
      if ( cur == end_stmt_it )
        break;
    }

    push_back_scan_t scan(cfunc);
    for ( cinsn_t *s : region )
      scan.apply_to(s, nullptr);

    libcxx_detail::vector_push_back_candidate_t cand;
    cand.elem_size = scan.elem_size;
    cand.max_size_constant = scan.max_size_const;
    cand.has_throw_length_error = scan.has_throw_length_error;
    cand.has_bad_array_new_length = scan.has_bad_array_new_length;
    cand.has_operator_new = scan.has_operator_new;
    cand.has_backward_copy_loop = scan.has_backward_copy_loop;
    cand.has_old_buffer_free = scan.has_old_buffer_free;

    // Resolve vector base slot from `__throw_length_error(vec_arg)`.
    slot_loc_t begin_slot;
    cexpr_t *vec_arg_clean = nullptr;
    if ( scan.throw_len_vec_arg != nullptr )
    {
      const cexpr_t *va = strip_casts(scan.throw_len_vec_arg);
      if ( va != nullptr && va->op == cot_ref && va->x != nullptr )
      {
        begin_slot = extract_slot_loc(cfunc, va->x);
        vec_arg_clean = new cexpr_t(*va);
      }
      else if ( va != nullptr && va->op == cot_var )
      {
        begin_slot.valid = true;
        begin_slot.is_stk = false;
        begin_slot.lvar = int(va->v.idx);
        begin_slot.offset = 0;
        begin_slot.raw_expr = va;
        vec_arg_clean = new cexpr_t(*va);
      }
    }

    if ( !pre_cap_slot.valid )
      pre_cap_slot = extract_slot_loc(cfunc, cond->y);
    if ( !pre_end_slot.valid )
      pre_end_slot = extract_slot_loc(cfunc, cond->x);

    int64 end_delta = 0;
    int64 cap_delta = 0;
    if ( slot_delta_bytes(begin_slot, pre_end_slot, &end_delta) )
      cand.end_offset_from_begin = end_delta;
    if ( slot_delta_bytes(begin_slot, pre_cap_slot, &cap_delta) )
      cand.cap_offset_from_begin = cap_delta;

    cand.has_capacity_check = (is_form_a || is_form_b) && pre_end_slot.valid && pre_cap_slot.valid;
    cand.has_fast_path_store_and_advance = val_expr != nullptr;
    cand.has_new_elem_store = val_expr != nullptr && v_val_temp >= 0;
    cand.has_vector_triple_update = begin_slot.valid && pre_end_slot.valid && pre_cap_slot.valid;

    if ( end_stmt_it->op == cit_expr && end_stmt_it->cexpr != nullptr
         && end_stmt_it->cexpr->op == cot_asg )
    {
      const slot_loc_t final_lhs = extract_slot_loc(cfunc, end_stmt_it->cexpr->x);
      if ( same_slot(final_lhs, pre_end_slot) )
        cand.has_final_end_store = true;
    }

    cand.has_external_incoming_label = has_external_goto_into(region, true);

    std::set<int> temps;
    if ( v_val_temp >= 0 )
      temps.insert(v_val_temp);
    if ( v_end_temp >= 0 )
      temps.insert(v_end_temp);
    if ( v_cap_temp >= 0 )
      temps.insert(v_cap_temp);
    cand.temps_do_not_escape = region_temps_do_not_escape(region, temps);

    if ( !libcxx_detail::verify_vector_push_back_realloc_site(cand) || vec_arg_clean == nullptr
         || val_expr == nullptr )
    {
      delete vec_arg_clean;
      ++stats.skipped[3];
      return false;
    }

    const ea_t ea = find_valid_ea(*it, cfunc->entry_ea);
    cexpr_t *val_clone = new cexpr_t(*strip_casts(val_expr));
    cexpr_t *call
      = make_void_helper_call("std::vector<T>::push_back", { vec_arg_clean, val_clone }, ea);
    if ( call == nullptr )
    {
      delete vec_arg_clean;
      delete val_clone;
      ++stats.skipped[3];
      return false;
    }

    const int preserved_entry_label = start_stmt_it->label_num;
    for ( auto cur = next_insn_it(start_stmt_it);; ++cur )
    {
      clear_labels_recursive(&*cur);
      to_remove.insert(&*cur);
      if ( cur == end_stmt_it )
        break;
    }
    clear_labels_recursive(&*start_stmt_it);
    start_stmt_it->label_num = preserved_entry_label;
    replace_insn_with_call(&*start_stmt_it, call, cfunc->entry_ea);
    ++stats.matched[3];
    return true;
  }

public:
  explicit wasmhex_libcxx_rewriter_t(cfunc_t *_cfunc) : ctree_visitor_t(CV_POST), cfunc(_cfunc) {}

  int idaapi visit_insn(cinsn_t *insn) override
  {
    if ( insn->op != cit_block || insn->cblock == nullptr )
      return 0;
    cblock_t *b = insn->cblock;
    for ( auto it = b->begin(); it != b->end(); ++it )
    {
      if ( to_remove.count(&*it) != 0 )
        continue;
      if ( try_match_vector_push_back(b, it) )
        continue;
      if ( try_match_vector_string_dtor(b, it) )
        continue;
      if ( try_match_unique_ptr_delete(&*it) )
        continue;
      if ( try_match_sso_string_dtor(&*it) )
        continue;
    }
    return 0;
  }

  void apply()
  {
    load_libcxx_marks();
    func_info.apply_to(&cfunc->body, nullptr);
    apply_to(&cfunc->body, nullptr);
    remove_block_statements(cfunc, to_remove);
    cfunc->remove_unused_labels();
    if ( options::debug("cpp") )
    {
      msg("WASMHEX_LIBCXX_STATS entry=%a "
          "unique_ptr_delete=%zu/%zu "
          "sso_string_dtor=%zu/%zu "
          "vector_string_dtor=%zu/%zu "
          "vector_push_back=%zu/%zu "
          "sso_mark_hits=%zu\n",
          cfunc->entry_ea, stats.matched[0], stats.skipped[0], stats.matched[1], stats.skipped[1],
          stats.matched[2], stats.skipped[2], stats.matched[3], stats.skipped[3],
          stats.sso_mark_hits);
      // Telemetry mirror on stderr (msg() is not visible in idalib batch).
      if ( stats.sso_mark_hits != 0 )
      {
        char mark_line[160];
        const int n2
          = qsnprintf(mark_line, sizeof(mark_line), "WASMHEX_LIBCXX_MARKHITS entry=%a hits=%zu\n",
                      cfunc->entry_ea, stats.sso_mark_hits);
        if ( n2 > 0 )
          ::write(2, mark_line, size_t(n2));
      }
    }
  }
};

ssize_t idaapi libcxx_hexrays_callback(void *, hexrays_event_t event, va_list va)
{
  if ( event == hxe_maturity )
  {
    cfunc_t *cfunc = va_arg(va, cfunc_t *);
    const int maturity = va_arg(va, int);
    if ( maturity == CMAT_FINAL && cfunc != nullptr && !options::debug("norewriters")
         && !options::debug("nocpp") )
    {
      netnode wasm_node(wasmhex_node_name, 0, false);
      wasmhex_bounds_mode_t mode = wasmhex_bounds_mode_t::explicit_bounds;
      if ( wasm_node != BADNODE && get_wasm_bounds_mode(wasm_node, &mode)
           && mode == wasmhex_bounds_mode_t::hidden )
      {
        wasmhex_libcxx_rewriter_t rewriter(cfunc);
        rewriter.apply();
      }
    }
  }
  return 0;
}

bool &libcxx_callback_installed()
{
  static bool installed = false;
  return installed;
}

struct libcxx_idp_listener_t final : public event_listener_t
{
  bool hooked = false;
  ssize_t idaapi on_event(ssize_t code, va_list) override
  {
    if ( code == processor_t::ev_term )
    {
      if ( libcxx_callback_installed() && get_hexdsp() != nullptr )
      {
        remove_hexrays_callback(libcxx_hexrays_callback, nullptr);
        libcxx_callback_installed() = false;
      }
      if ( hooked )
      {
        unhook_event_listener(HT_IDP, this);
        hooked = false;
      }
    }
    return 0;
  }
};

libcxx_idp_listener_t &libcxx_idp_listener()
{
  static libcxx_idp_listener_t listener;
  return listener;
}

} // namespace

void ensure_libcxx_rewriter_installed()
{
  // Callback ordering: the engine invokes hexrays callbacks in REVERSE
  // installation order (LIFO; probe: install A,B -> invoked B,A). The
  // remove/reinstall dance below is therefore load-bearing: it re-appends
  // wasmhex_hexrays_callback AFTER libcxx_hexrays_callback so that, per
  // LIFO, the main ctree callback is invoked FIRST at CMAT_FINAL and the
  // libc++ matcher runs immediately AFTER it (its patterns expect the
  // post-EH/post-CPP rewritten shape). The naive "install libcxx once
  // before the main callback at ev_newfile" alternative cannot replicate
  // today's effective invocation order (main -> libcxx -> go_type_notes ->
  // go_structural -> float): under LIFO the float and Go callbacks
  // (installed later, some lazily and file-local to filter.cpp) would flip
  // to run BEFORE the main rewriters.
  if ( !libcxx_callback_installed() && get_hexdsp() != nullptr )
  {
    remove_hexrays_callback(wasmhex_hexrays_callback, nullptr);
    libcxx_callback_installed() = install_hexrays_callback(libcxx_hexrays_callback, nullptr);
    install_hexrays_callback(wasmhex_hexrays_callback, nullptr);
    if ( libcxx_callback_installed() && !libcxx_idp_listener().hooked )
      libcxx_idp_listener().hooked = hook_event_listener(HT_IDP, &libcxx_idp_listener(), nullptr);
  }
}

} // namespace wasmhex

#endif // WASMHEX_PURE_STRING_TEST
