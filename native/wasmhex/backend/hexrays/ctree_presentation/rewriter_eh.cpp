#include "../../../core/binary/instruction_stream.hpp"

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <unistd.h>

namespace wasmhex
{
namespace eh_detail
{

// Structured EH region classifier and provable empty-cleanup
// landing-pad collapse rules.
enum class eh_pad_skip_reason_t : std::uint8_t
{
  none = 0,
  missing_catch_call,
  missing_resume_or_trap,
  non_call_or_assign_cleanup,
  non_empty_cleanup,
  unproven_predecessor_edge,
  non_adjacent_invoke_guard,
  escaping_pad_temp,
  count
};

inline const char *eh_pad_skip_reason_name(eh_pad_skip_reason_t reason)
{
  switch ( reason )
  {
    case eh_pad_skip_reason_t::none: return "none";
    case eh_pad_skip_reason_t::missing_catch_call: return "missing_catch_call";
    case eh_pad_skip_reason_t::missing_resume_or_trap: return "missing_resume_or_trap";
    case eh_pad_skip_reason_t::non_call_or_assign_cleanup: return "non_call_or_assign_cleanup";
    case eh_pad_skip_reason_t::non_empty_cleanup: return "non_empty_cleanup";
    case eh_pad_skip_reason_t::unproven_predecessor_edge: return "unproven_predecessor_edge";
    case eh_pad_skip_reason_t::non_adjacent_invoke_guard: return "non_adjacent_invoke_guard";
    case eh_pad_skip_reason_t::escaping_pad_temp: return "escaping_pad_temp";
    case eh_pad_skip_reason_t::count: break;
  }
  return "unknown";
}

struct eh_landing_pad_candidate_t
{
  bool starts_with_find_matching_catch = false;
  bool is_catch_all = false;
  bool ends_with_resume_and_trap = false;
  bool resume_uses_catch_var = false;
  std::size_t cleanup_stmt_count = 0;
  bool all_cleanup_stmts_calls_or_assigns = true;
  std::size_t guard_predecessor_count = 0;
  std::size_t non_guard_predecessor_count = 0;
  bool all_guards_adjacent_to_invoke = false;
  bool catch_temp_does_not_escape = false;

  // Move-analysis inputs (filled by structure_eh_regions):
  bool entry_owned_by_guard = false;    // catch is block-first inside a guard's
                                        // then-branch (owner guard is exact)
  std::size_t shared_body_stmts = 0;    // traced stmts also traced for another pad
  std::size_t exterior_label_jumps = 0; // external gotos to interior body labels
  std::uint8_t pred_blocker = 0;        // eh_pred_blocker_t (raw to stay POD-testable)
};

// Rule 1: a block is a valid Emscripten EH landing pad
// when it starts with `v_exc = __cxa_find_matching_catch_*()`, executes only
// call/assignment cleanup statements (or destructor helper blocks), and
// terminates in `__resumeException(v_exc); __builtin_trap();`.
inline bool is_valid_eh_landing_pad(const eh_landing_pad_candidate_t &c)
{
  return c.starts_with_find_matching_catch && c.ends_with_resume_and_trap
    && c.all_cleanup_stmts_calls_or_assigns;
}

// Rule 2: an EH landing pad and its
// incoming `if (*(int *)GOT.mem.__THREW__ == 1)` guard(s) collapse atomically
// into `__cxa_rethrow();` when and only when:
//   1. The pad is a valid catch-all landing pad (`__cxa_find_matching_catch_2()`)
//      whose `v_exc` is passed directly to `__resumeException(v_exc);
//      __builtin_trap();`.
//   2. Cleanup is empty (`cleanup_stmt_count == 0`).
//   3. Every incoming edge to the pad is an exact `__THREW__ == 1` guard
//      (`guard_predecessor_count >= 1 && non_guard_predecessor_count == 0`).
//   4. Every guard is immediately adjacent to its guarded invoke call
//      (`all_guards_adjacent_to_invoke`).
//   5. `v_exc` has no uses outside the landing pad (`catch_temp_does_not_escape`).
inline eh_pad_skip_reason_t classify_eh_pad_collapse(const eh_landing_pad_candidate_t &c)
{
  if ( !c.starts_with_find_matching_catch || !c.is_catch_all )
    return eh_pad_skip_reason_t::missing_catch_call;
  if ( !c.ends_with_resume_and_trap || !c.resume_uses_catch_var )
    return eh_pad_skip_reason_t::missing_resume_or_trap;
  if ( !c.all_cleanup_stmts_calls_or_assigns )
    return eh_pad_skip_reason_t::non_call_or_assign_cleanup;
  if ( c.cleanup_stmt_count != 0 )
    return eh_pad_skip_reason_t::non_empty_cleanup;
  if ( c.guard_predecessor_count == 0 || c.non_guard_predecessor_count != 0 )
    return eh_pad_skip_reason_t::unproven_predecessor_edge;
  if ( !c.all_guards_adjacent_to_invoke )
    return eh_pad_skip_reason_t::non_adjacent_invoke_guard;
  if ( !c.catch_temp_does_not_escape )
    return eh_pad_skip_reason_t::escaping_pad_temp;
  return eh_pad_skip_reason_t::none;
}

inline bool verify_eh_empty_pad_collapse(const eh_landing_pad_candidate_t &c)
{
  return classify_eh_pad_collapse(c) == eh_pad_skip_reason_t::none;
}

enum class eh_pad_move_plan_t : std::uint8_t
{
  not_candidate = 0,
  already_inline,
  unproven_pred_edge,
  shared_cleanup_tail,
  exterior_jump_in,
  movable,
  count
};

// Structural sub-reason recorded for pads classified
// unproven_pred_edge (why the incoming edges failed the exact-guard proof).
enum class eh_pred_blocker_t : std::uint8_t
{
  none = 0,
  owner_guard_not_exact, // pad block owned by an if whose condition is not an
                         // exact `__THREW__ == 1` guard (vector-realloc
                         // `!= 1` + trap paths, throw-helper blocks)
  catch_not_block_first, // catch is not the first statement of its block
  plain_block_fallin,    // pad in a plain block: fall-in from the preceding
                         // structure (loop-break edges) not disproven
  non_guard_gotos,       // gotos to the entry label from non-guard sites
  nonadj_guard,          // a guard is not adjacent to its invoke call
  count
};

inline const char *eh_pad_move_plan_name(eh_pad_move_plan_t plan)
{
  switch ( plan )
  {
    case eh_pad_move_plan_t::not_candidate: return "not_candidate";
    case eh_pad_move_plan_t::already_inline: return "already_inline";
    case eh_pad_move_plan_t::unproven_pred_edge: return "unproven_pred_edge";
    case eh_pad_move_plan_t::shared_cleanup_tail: return "shared_cleanup_tail";
    case eh_pad_move_plan_t::exterior_jump_in: return "exterior_jump_in";
    case eh_pad_move_plan_t::movable: return "movable";
    case eh_pad_move_plan_t::count: break;
  }
  return "unknown";
}

inline eh_pad_move_plan_t classify_eh_pad_move(const eh_landing_pad_candidate_t &c)
{
  if ( !is_valid_eh_landing_pad(c) || c.cleanup_stmt_count == 0 )
    return eh_pad_move_plan_t::not_candidate;
  // Pads already sitting inside their owning exact guard are in final shape
  // (shared cleanup tails below them do not change that).
  if ( c.entry_owned_by_guard )
    return eh_pad_move_plan_t::already_inline;
  if ( c.non_guard_predecessor_count != 0 || c.guard_predecessor_count == 0
       || !c.all_guards_adjacent_to_invoke )
  {
    return eh_pad_move_plan_t::unproven_pred_edge;
  }
  if ( c.shared_body_stmts != 0 )
    return eh_pad_move_plan_t::shared_cleanup_tail;
  if ( c.exterior_label_jumps != 0 )
    return eh_pad_move_plan_t::exterior_jump_in;
  return eh_pad_move_plan_t::movable;
}

struct eh_region_stats_t
{
  std::size_t guards_total = 0;
  std::size_t guards_delimited = 0;
  std::size_t guards_collapsed = 0;
  std::size_t pads_annotated = 0;
  std::size_t pads_collapsed = 0;
  std::size_t pads_skipped_by_reason[std::size_t(eh_pad_skip_reason_t::count)] = {};
  // Per-plan pad counts for the non-empty structuring analysis.
  std::size_t pads_by_move_plan[std::size_t(eh_pad_move_plan_t::count)] = {};
  std::size_t pads_label_tail_entry = 0; // valid pads in plain tail blocks (no owner guard)
  // Structural blocker breakdown for unproven_pred_edge pads.
  std::size_t pred_blockers[std::size_t(eh_pred_blocker_t::count)] = {};

  std::size_t total_pads_skipped() const
  {
    std::size_t sum = 0;
    for ( std::size_t i = 1; i < std::size_t(eh_pad_skip_reason_t::count); ++i )
      sum += pads_skipped_by_reason[i];
    return sum;
  }
};

} // namespace eh_detail
} // namespace wasmhex

#ifndef WASMHEX_PURE_STRING_TEST

#include "ctree.hpp"

#include "../../../ida/loader/options.hpp"
#include "../../../core/toolchains/go/eh.hpp"

#include <bytes.hpp>
#include <name.hpp>
#include <segment.hpp>
#include <typeinf.hpp>

#include <map>
#include <set>
#include <vector>

#include "ctree_types.hpp"

namespace wasmhex
{

extern "C" __attribute__((used)) const char wasmhex_s3_build_marker[]
  = "WASMHEX_BUILD_MARKER_M4_S3_LEGALIZE_I64_EH_CLEANUP";
extern "C" __attribute__((used)) const char wasmhex_c5_build_marker[]
  = "WASMHEX_BUILD_MARKER_C5_EH_REGIONS_COLLAPSE";
extern "C" __attribute__((used)) const char wasmhex_c5full_build_marker[]
  = "WASMHEX_BUILD_MARKER_C5_FULL_EH_MOVE_ANALYSIS";

namespace
{

} // namespace

// Erase the given statements from any block (defined in ctree.cpp
// shared by the rewriter translation units).
void remove_block_statements(cfunc_t *cfunc, const std::set<cinsn_t *> &targets);
// The EH rewriter only removes stores PROVEN to bind the Emscripten
// __THREW__ protocol - direct exact-name flag stores, or dereferences of a
// local whose provenance (assignment from the flag, no overwrite) was
// established in the same block. Unrelated `*p = 0` stores before an invoke_
// call are left alone.
class wasmhex_eh_rewriter_t final : public ctree_visitor_t
{
  cfunc_t *cfunc;
  std::set<cinsn_t *> to_remove;

  static bool is_threw_name(const char *name)
  {
    return name != nullptr
      && (strcmp(name, "__THREW__") == 0 || strcmp(name, "GOT.mem.__THREW__") == 0
          || strcmp(name, "_THREW__") == 0 || strcmp(name, "GOT.mem._THREW__") == 0);
  }

  static const cexpr_t *strip_cast(const cexpr_t *e)
  {
    while ( e != nullptr && e->op == cot_cast && e->x != nullptr )
      e = e->x;
    return e;
  }

  static const cexpr_t *strip_callee_wrappers(const cexpr_t *e)
  {
    e = strip_cast(e);
    while ( e != nullptr && (e->op == cot_ref || e->op == cot_ptr) && e->x != nullptr )
      e = strip_cast(e->x);
    return e;
  }

  static bool is_threw_obj(const cexpr_t *e)
  {
    if ( e == nullptr )
      return false;
    e = strip_cast(e);
    if ( e->op == cot_obj )
    {
      qstring name;
      get_name(&name, e->obj_ea);
      return is_threw_name(name.c_str());
    }
    if ( e->op == cot_helper )
      return is_threw_name(e->helper);
    return false;
  }

  // Invoke identity is mark-first (loader blob 'i'): the mark set supersedes
  // the old strict-name fallback. Landing pads are mark-first too — the name
  // checks stay only as the fallback for unmarked pads.
  loader::eh_function_ir_t eh_ir;
  bool eh_ir_loaded = false;
  std::map<ea_t, const loader::eh_invoke_site_ir_t *> eh_invoke_by_ea;
  std::map<ea_t, const loader::eh_pad_ir_t *> eh_pad_by_catch_ea;
  std::map<ea_t, const loader::eh_pad_ir_t *> eh_pad_by_resume_ea;
  std::set<ea_t> eh_trap_eas;
  std::size_t eh_pad_ident_by_mark = 0;
  std::size_t eh_pad_ident_by_name = 0;

  void load_eh_marks()
  {
    if ( eh_ir_loaded )
      return;
    eh_ir_loaded = true;
    netnode wasm_node(wasmhex_node_name, 0, false);
    if ( wasm_node == BADNODE )
      return;
    bytevec_t blob;
    if ( wasm_node.getblob_ea(&blob, cfunc->entry_ea, 'i') <= 0 || blob.empty() )
      return;
    const std::size_t words = blob.size() / sizeof(std::uint32_t);
    std::vector<std::uint32_t> wire(words);
    std::memcpy(wire.data(), blob.begin(), words * sizeof(std::uint32_t));
    if ( !loader::decode_eh_function_ir_blob(wire.data(), wire.size(), &eh_ir) )
    {
      eh_ir = loader::eh_function_ir_t{};
      return;
    }
    for ( const loader::eh_invoke_site_ir_t &site : eh_ir.invokes )
      eh_invoke_by_ea.emplace(cfunc->entry_ea + site.call_offset, &site);
    for ( const loader::eh_pad_ir_t &pad : eh_ir.pads )
    {
      eh_pad_by_catch_ea.emplace(cfunc->entry_ea + pad.catch_offset, &pad);
      eh_pad_by_resume_ea.emplace(cfunc->entry_ea + pad.resume_offset, &pad);
      eh_trap_eas.insert(cfunc->entry_ea + pad.trap_offset);
    }
  }

  const loader::eh_invoke_site_ir_t *invoke_mark_for(const cexpr_t *call)
  {
    if ( call == nullptr || call->ea == BADADDR )
      return nullptr;
    const auto it = eh_invoke_by_ea.find(call->ea);
    return it == eh_invoke_by_ea.end() ? nullptr : it->second;
  }

  const loader::eh_pad_ir_t *pad_mark_for_catch(const cexpr_t *call)
  {
    if ( call == nullptr || call->ea == BADADDR )
      return nullptr;
    const auto it = eh_pad_by_catch_ea.find(call->ea);
    return it == eh_pad_by_catch_ea.end() ? nullptr : it->second;
  }

  const loader::eh_pad_ir_t *pad_mark_for_resume(const cexpr_t *call)
  {
    if ( call == nullptr || call->ea == BADADDR )
      return nullptr;
    const auto it = eh_pad_by_resume_ea.find(call->ea);
    return it == eh_pad_by_resume_ea.end() ? nullptr : it->second;
  }

  bool is_trap_mark(const cinsn_t &stmt)
  {
    if ( stmt.op != cit_expr || stmt.cexpr == nullptr )
      return false;
    const cexpr_t *call = stmt.cexpr;
    if ( call->op == cot_asg && call->y != nullptr )
      call = call->y;
    call = strip_cast(call);
    if ( call == nullptr || call->op != cot_call || call->ea == BADADDR )
      return false;
    return eh_trap_eas.count(call->ea) != 0;
  }

  bool is_invoke_call_expr(const cexpr_t *expr)
  {
    if ( expr == nullptr )
      return false;
    const cexpr_t *call = expr;
    if ( call->op == cot_asg && call->y != nullptr )
      call = call->y;
    call = strip_cast(call);
    if ( call->op != cot_call || call->x == nullptr )
      return false;
    return invoke_mark_for(call) != nullptr; // loader-proven invoke site
  }

  bool is_invoke_call(const cinsn_t &insn) // reads the mark map
  {
    if ( insn.op != cit_expr || insn.cexpr == nullptr )
      return false;
    return is_invoke_call_expr(insn.cexpr);
  }

  static bool is_zero_store_of(const cexpr_t *expr, int proved_lvar)
  {
    if ( expr == nullptr || expr->op != cot_asg || expr->y == nullptr || expr->y->op != cot_num
         || expr->y->numval() != 0 )
    {
      return false;
    }
    const cexpr_t *lhs = strip_cast(expr->x);
    if ( is_threw_obj(lhs) )
    {
      return true;
    }
    if ( lhs->op == cot_ptr && proved_lvar >= 0 )
    {
      const cexpr_t *base = strip_cast(lhs->x);
      if ( base != nullptr && base->op == cot_var && base->v.idx == proved_lvar )
      {
        return true;
      }
      // fall through: a cot_ptr store whose base is NOT the proved local
      // may still be a direct store through the flag itself.
    }
    // Direct protocol store through the flag itself, e.g.
    // `*(_DWORD *)GOT.mem.__THREW__ = 0;` - the Emscripten codegen with a
    // GOT import stores through the dereference, not through the object.
    if ( lhs->op == cot_ptr )
    {
      const cexpr_t *base = strip_cast(lhs->x);
      if ( is_threw_obj(base) )
      {
        return true;
      }
    }
    return false;
  }

  void fix_function_pointer(cexpr_t *e)
  {
    if ( e->op == cot_ptr && e->x != nullptr )
    {
      cexpr_t *inner = e->x;
      while ( inner->op == cot_cast && inner->x != nullptr )
        inner = inner->x;
      if ( inner->op == cot_obj )
      {
        const ea_t target_ea = inner->obj_ea;
        // Only for objects in a code segment - a plain function EA.
        // Data-segment objects (function-pointer tables, user structs) keep
        // their dereference untouched.
        segment_info_t seg;
        if ( get_segment_info(&seg, target_ea) && seg.get_type() == SEG_CODE
             && get_func_start(target_ea) != BADADDR )
        {
          cexpr_t *ref = make_ref(new cexpr_t(*inner));
          if ( ref != nullptr )
            e->replace_by(ref);
        }
      }
    }
  }

  // Collapse an Emscripten invoke shim (or legalize-i64 invoke
  // wrapper) into a direct call through its first argument when that argument
  // is a proven `&func` reference to a code function with a known prototype.
  // When the callee is a recognized legalize-i64 wrapper (`wasm_func_62934`
  // etc.) and the first argument is a `GOT.func.[orig$]_Z...` global or a
  // dynamic pointer, rewrite the wrapper callee to its underlying `invoke_*`
  // helper so the C++ rewriter's `rewrite_invoke_got` / throw pass handles it
  // identically to direct `invoke_*` calls.
  bool try_direct_call_expr(cexpr_t *root)
  {
    if ( root == nullptr )
      return false;
    cexpr_t *call = root;
    if ( call->op == cot_asg && call->y != nullptr )
      call = call->y;
    call = const_cast<cexpr_t *>(strip_cast(call));
    if ( call->op != cot_call || call->x == nullptr || call->a == nullptr || call->a->size() < 1 )
      return false;
    qstring wrapped_invoke;
    // The loader mark is the ONLY invoke-identity gate: unmarked calls are
    // not invokes — never attempt a collapse without one.
    const loader::eh_invoke_site_ir_t *mark = invoke_mark_for(call);
    if ( mark == nullptr )
      return false;
    const bool is_legalize_wrapper = mark->wrapped_invoke_fn != UINT32_MAX;
    if ( is_legalize_wrapper )
    {
      netnode wasm_node(wasmhex_node_name, 0, false);
      if ( wasm_node == BADNODE
           || wasm_node.supstr(&wrapped_invoke, uval_t(mark->wrapped_invoke_fn), 'H') <= 0 )
        return false;
    }

    // Provenance rewriting may not have run yet (visit_insn precedes
    // leave_expr), so accept the raw `*func` deref, the `&func` ref, or a
    // bare code object — all select the same code function below.
    const cexpr_t *first = strip_cast(&(*call->a)[0]);
    const cexpr_t *target = nullptr;
    if ( first != nullptr && first->op == cot_ref )
      target = strip_cast(first->x);
    else if ( first != nullptr && first->op == cot_ptr )
      target = strip_cast(first->x);
    else if ( first != nullptr && first->op == cot_obj )
      target = first;
    if ( target != nullptr && target->op == cot_obj )
    {
      const ea_t func_ea = target->obj_ea;
      segment_info_t seg;
      if ( get_segment_info(&seg, func_ea) && seg.get_type() == SEG_CODE
           && get_func_start(func_ea) != BADADDR )
      {
        tinfo_t ftype;
        tinfo_t ptr_type;
        if ( get_tinfo(&ftype, func_ea) && ftype.is_func() && ptr_type.create_ptr(ftype) )
        {
          cexpr_t *ref = new cexpr_t();
          ref->op = cot_ref;
          ref->x = new cexpr_t(*target);
          ref->ea = call->ea;
          ref->type = ptr_type;

          carglist_t *arguments = new carglist_t(ftype, 0);
          for ( size_t index = 1; index < call->a->size(); ++index )
          {
            carg_t argument;
            argument.consume_cexpr(new cexpr_t((*call->a)[index]));
            arguments->push_back(argument);
          }
          cexpr_t *direct = new cexpr_t();
          direct->op = cot_call;
          direct->x = ref;
          direct->a = arguments;
          direct->ea = call->ea;
          direct->type = ftype.get_rettype();
          if ( direct->type.empty() )
            direct->type = call->type;
          call->replace_by(direct);
          return true;
        }
      }
    }

    if ( is_legalize_wrapper && !wrapped_invoke.empty() )
    {
      cexpr_t *helper = create_helper(true, call->x->type, "%s", wrapped_invoke.c_str());
      if ( helper != nullptr )
      {
        helper->ea = call->x->ea;
        call->x->replace_by(helper);
        return true;
      }
    }
    return false;
  }

  bool try_direct_call(cinsn_t &stmt) { return try_direct_call_expr(stmt.cexpr); }

  // Whole-function lvar use counts (computed once before rewriting) and
  // the uses living inside statements slated for removal. A definition is
  // dropped only when every use of its lvar vanishes together with it —
  // block-local scanning is unsound because lvar scope is the function.
  std::map<int, size_t> func_total_uses;
  std::map<int, size_t> removal_uses;

  // Count variable USES (a plain `vN = ...` target is a definition, not
  // a use; compound assignments — cot_asgadd & friends — read the target and
  // recurse normally).
  struct use_counter_t final : ctree_visitor_t
  {
    std::map<int, size_t> &counts;
    explicit use_counter_t(std::map<int, size_t> &c) : ctree_visitor_t(CV_FAST), counts(c) {}
    int idaapi visit_expr(cexpr_t *e) override
    {
      if ( e->op == cot_asg && e->x != nullptr && e->x->op == cot_var )
      {
        if ( e->y != nullptr )
        {
          use_counter_t rhs(counts);
          rhs.apply_to(e->y, nullptr);
        }
        return 1; // children handled manually
      }
      if ( e->op == cot_var )
        ++counts[int(e->v.idx)];
      return 0;
    }
  };

  void add_removed_expr_uses(cexpr_t *expr)
  {
    if ( expr == nullptr )
      return;
    std::map<int, size_t> expr_counts;
    use_counter_t counter(expr_counts);
    counter.apply_to(expr, nullptr);
    for ( const auto &kv : expr_counts )
      removal_uses[kv.first] += kv.second;
  }

  bool all_uses_vanish(int lvar, size_t replaced_uses) const
  {
    if ( lvar < 0 )
      return false;
    const auto tit = func_total_uses.find(lvar);
    const size_t total = tit == func_total_uses.end() ? 0 : tit->second;
    const auto rit = removal_uses.find(lvar);
    const size_t removed = rit == removal_uses.end() ? 0 : rit->second;
    return removed + replaced_uses >= total;
  }

  static cexpr_t *build_threw_read_clone(const cexpr_t *threw_expr, const cexpr_t *ptr_origin,
                                         int threw_ptr_lvar)
  {
    cexpr_t *read_clone = new cexpr_t(*threw_expr);
    if ( ptr_origin != nullptr && threw_ptr_lvar >= 0 )
    {
      struct origin_swapper_t final : ctree_visitor_t
      {
        int target;
        const cexpr_t *origin;
        origin_swapper_t(int t, const cexpr_t *o) : ctree_visitor_t(CV_FAST), target(t), origin(o)
        {
        }
        int idaapi visit_expr(cexpr_t *e) override
        {
          if ( e->op == cot_var && int(e->v.idx) == target )
            e->replace_by(new cexpr_t(*origin));
          return 0;
        }
      };
      origin_swapper_t swapper(threw_ptr_lvar, ptr_origin);
      swapper.apply_to(read_clone, nullptr);
    }
    return read_clone;
  }

  // Recursively walk boolean condition connectives (`||`, `&&`,
  // `!`) and replace `read_lvar == 1` / `read_lvar != 1` operands with a
  // direct `*(int *)GOT.mem.__THREW__` clone.
  static size_t replace_threw_read_in_cond(cexpr_t *cond, int read_lvar,
                                           const cexpr_t &read_template)
  {
    if ( cond == nullptr || read_lvar < 0 )
      return 0;
    if ( cond->op == cot_lor || cond->op == cot_land )
    {
      return replace_threw_read_in_cond(cond->x, read_lvar, read_template)
        + replace_threw_read_in_cond(cond->y, read_lvar, read_template);
    }
    if ( cond->op == cot_lnot )
      return replace_threw_read_in_cond(cond->x, read_lvar, read_template);
    if ( cond->op == cot_eq || cond->op == cot_ne )
    {
      if ( cond->x != nullptr && cond->x->op == cot_var && int(cond->x->v.idx) == read_lvar )
      {
        cond->x->replace_by(new cexpr_t(read_template));
        return 1;
      }
      if ( cond->y != nullptr && cond->y->op == cot_var && int(cond->y->v.idx) == read_lvar )
      {
        cond->y->replace_by(new cexpr_t(read_template));
        return 1;
      }
    }
    return 0;
  }

  static void flatten_comma_chain(cexpr_t *e, std::vector<cexpr_t *> *out)
  {
    if ( e == nullptr )
      return;
    if ( e->op == cot_comma )
    {
      flatten_comma_chain(e->x, out);
      flatten_comma_chain(e->y, out);
    }
    else
    {
      out->push_back(e);
    }
  }

  // Clean Emscripten `__THREW__` protocol sequences embedded in
  // comma chains inside `if` conditions (e.g. the RHS of `||` in
  // `AddRelationTdom` and `ExtractJoinRelations`). Fail-closed: any local
  // whose use count does not vanish completely after replacing the condition
  // read keeps its definition intact.
  void clean_comma_chain(cexpr_t *comma_root)
  {
    if ( comma_root == nullptr || comma_root->op != cot_comma )
      return;
    std::vector<cexpr_t *> items;
    flatten_comma_chain(comma_root, &items);
    if ( items.size() < 2 )
      return;

    clean_if_condition(items.back());

    std::set<size_t> removed_indices;
    auto mark_item_removed = [&](size_t idx)
    {
      if ( idx >= items.size() - 1 || !removed_indices.insert(idx).second )
        return;
      add_removed_expr_uses(items[idx]);
    };

    for ( size_t call_pos = 0; call_pos + 1 < items.size(); ++call_pos )
    {
      if ( removed_indices.count(call_pos) != 0 || !is_invoke_call_expr(items[call_pos]) )
        continue;

      int threw_ptr_lvar = -1;
      int ptr_assign_idx = -1;
      for ( size_t k = 0; k < call_pos; ++k )
      {
        if ( removed_indices.count(k) != 0 )
          continue;
        cexpr_t *expr = items[k];
        if ( expr == nullptr || expr->op != cot_asg || expr->x == nullptr )
          continue;
        const cexpr_t *lhs = strip_cast(expr->x);
        if ( lhs->op != cot_var )
          continue;
        if ( is_threw_obj(expr->y) )
        {
          threw_ptr_lvar = int(lhs->v.idx);
          ptr_assign_idx = int(k);
        }
        else if ( int(lhs->v.idx) == threw_ptr_lvar && threw_ptr_lvar >= 0 )
        {
          threw_ptr_lvar = -1;
          ptr_assign_idx = -1;
        }
      }

      int zero_before_idx = -1;
      for ( size_t k = 0; k < call_pos; ++k )
      {
        if ( removed_indices.count(k) != 0 )
          continue;
        if ( is_zero_store_of(items[k], threw_ptr_lvar) )
          zero_before_idx = int(k);
      }
      if ( zero_before_idx < 0 )
        continue;

      try_direct_call_expr(items[call_pos]);

      int read_lvar = -1;
      int read_idx = -1;
      int zero_after_idx = -1;
      const cexpr_t *threw_expr = nullptr;

      for ( size_t k = call_pos + 1; k + 1 < items.size(); ++k )
      {
        if ( removed_indices.count(k) != 0 )
          continue;
        if ( is_invoke_call_expr(items[k]) )
          break;
        cexpr_t *expr = items[k];
        if ( expr == nullptr || expr->op != cot_asg )
          continue;
        if ( is_zero_store_of(expr, threw_ptr_lvar) )
        {
          zero_after_idx = int(k);
        }
        else if ( expr->x != nullptr && expr->x->op == cot_var )
        {
          const cexpr_t *rhs = strip_cast(expr->y);
          if ( rhs == nullptr )
            continue;
          if ( is_threw_obj(rhs) )
          {
            read_lvar = int(expr->x->v.idx);
            read_idx = int(k);
            if ( threw_expr == nullptr )
              threw_expr = rhs;
          }
          else if ( rhs->op == cot_ptr && threw_ptr_lvar >= 0 )
          {
            const cexpr_t *base = strip_cast(rhs->x);
            if ( base != nullptr && base->op == cot_var && int(base->v.idx) == threw_ptr_lvar )
            {
              read_lvar = int(expr->x->v.idx);
              read_idx = int(k);
              if ( threw_expr == nullptr )
                threw_expr = rhs;
            }
          }
        }
      }

      mark_item_removed(size_t(zero_before_idx));
      if ( zero_after_idx >= 0 )
        mark_item_removed(size_t(zero_after_idx));

      if ( read_idx >= 0 && read_lvar >= 0 && threw_expr != nullptr )
      {
        const cexpr_t *ptr_origin
          = (ptr_assign_idx >= 0 && items[size_t(ptr_assign_idx)] != nullptr)
          ? items[size_t(ptr_assign_idx)]->y
          : nullptr;
        cexpr_t *read_clone = build_threw_read_clone(threw_expr, ptr_origin, threw_ptr_lvar);
        const size_t replaced = replace_threw_read_in_cond(items.back(), read_lvar, *read_clone);
        delete read_clone;
        if ( replaced > 0 )
        {
          if ( all_uses_vanish(read_lvar, replaced) )
            mark_item_removed(size_t(read_idx));
          if ( ptr_assign_idx >= 0 && all_uses_vanish(threw_ptr_lvar, 0) )
            mark_item_removed(size_t(ptr_assign_idx));
        }
      }
    }

    if ( removed_indices.empty() )
      return;

    std::vector<cexpr_t *> kept;
    kept.reserve(items.size() - removed_indices.size());
    for ( size_t i = 0; i < items.size(); ++i )
      if ( removed_indices.count(i) == 0 )
        kept.push_back(items[i]);
    if ( kept.empty() )
      return;

    cexpr_t *rebuilt = new cexpr_t(*kept[0]);
    for ( size_t i = 1; i < kept.size(); ++i )
    {
      cexpr_t *rhs = new cexpr_t(*kept[i]);
      cexpr_t *comma = new cexpr_t(cot_comma, rebuilt, rhs);
      comma->ea = rhs->ea != BADADDR ? rhs->ea : comma_root->ea;
      comma->type = rhs->type;
      rebuilt = comma;
    }
    comma_root->replace_by(rebuilt);
  }

  void clean_if_condition(cexpr_t *cond)
  {
    if ( cond == nullptr )
      return;
    if ( cond->op == cot_lor || cond->op == cot_land )
    {
      clean_if_condition(cond->x);
      clean_if_condition(cond->y);
    }
    else if ( cond->op == cot_lnot )
    {
      clean_if_condition(cond->x);
    }
    else if ( cond->op == cot_comma )
    {
      clean_comma_chain(cond);
    }
  }

  static bool is_find_matching_catch_name(const char *name)
  {
    if ( name == nullptr )
      return false;
    while ( *name == '_' )
      ++name;
    return strncmp(name, "cxa_find_matching_catch", 23) == 0;
  }

  static bool is_find_matching_catch_2_name(const char *name)
  {
    if ( name == nullptr )
      return false;
    while ( *name == '_' )
      ++name;
    return strcmp(name, "cxa_find_matching_catch_2") == 0;
  }

  static bool is_resume_exception_name(const char *name)
  {
    if ( name == nullptr )
      return false;
    while ( *name == '_' )
      ++name;
    return strcmp(name, "resumeException") == 0;
  }

  static bool is_builtin_trap_name(const char *name)
  {
    if ( name == nullptr )
      return false;
    return strcmp(name, "__builtin_trap") == 0 || strcmp(name, "_builtin_trap") == 0;
  }

  static bool stmt_call_of(const cinsn_t &stmt, const cexpr_t **out_call)
  {
    if ( out_call != nullptr )
      *out_call = nullptr;
    if ( stmt.op != cit_expr || stmt.cexpr == nullptr )
      return false;
    const cexpr_t *call = stmt.cexpr;
    if ( call->op == cot_asg && call->y != nullptr )
      call = call->y;
    call = strip_cast(call);
    if ( call == nullptr || call->op != cot_call || call->x == nullptr )
      return false;
    if ( out_call != nullptr )
      *out_call = call;
    return true;
  }

  bool stmt_catches_named_or_marked(const cinsn_t &stmt)
  {
    const cexpr_t *call = nullptr;
    if ( !stmt_call_of(stmt, &call) )
      return false;
    if ( pad_mark_for_catch(call) != nullptr )
    {
      ++eh_pad_ident_by_mark;
      return true;
    }
    const bool named = stmt_calls_named(stmt, is_find_matching_catch_name);
    if ( named )
      ++eh_pad_ident_by_name;
    return named;
  }

  bool stmt_resumes_named_or_marked(const cinsn_t &stmt)
  {
    const cexpr_t *call = nullptr;
    if ( !stmt_call_of(stmt, &call) )
      return false;
    if ( pad_mark_for_resume(call) != nullptr )
    {
      ++eh_pad_ident_by_mark;
      return true;
    }
    const bool named = stmt_calls_named(stmt, is_resume_exception_name);
    if ( named )
      ++eh_pad_ident_by_name;
    return named;
  }

  static bool stmt_calls_named(const cinsn_t &stmt, bool (*pred)(const char *))
  {
    if ( stmt.op != cit_expr || stmt.cexpr == nullptr )
      return false;
    const cexpr_t *call = stmt.cexpr;
    if ( call->op == cot_asg && call->y != nullptr )
      call = call->y;
    call = strip_cast(call);
    if ( call->op != cot_call || call->x == nullptr )
      return false;
    const cexpr_t *callee = strip_callee_wrappers(call->x);
    if ( callee == nullptr )
      return false;
    if ( callee->op == cot_obj )
    {
      qstring name;
      get_name(&name, callee->obj_ea);
      return pred(name.c_str());
    }
    if ( callee->op == cot_helper )
      return pred(callee->helper);
    return false;
  }

  bool is_catch_assign_stmt(const cinsn_t &stmt, int *out_catch_lvar, bool *out_is_catch_all)
  {
    if ( stmt.op != cit_expr || stmt.cexpr == nullptr || stmt.cexpr->op != cot_asg
         || stmt.cexpr->x == nullptr || stmt.cexpr->y == nullptr )
      return false;
    const cexpr_t *lhs = strip_cast(stmt.cexpr->x);
    const cexpr_t *call = strip_cast(stmt.cexpr->y);
    if ( lhs == nullptr || lhs->op != cot_var || call == nullptr || call->op != cot_call
         || call->x == nullptr )
      return false;
    // mark-first: the pad record proves this call EA is a catch site and
    // carries is_catch_all from the callee import/export name
    const loader::eh_pad_ir_t *pad = pad_mark_for_catch(call);
    if ( pad != nullptr )
    {
      if ( pad->is_catch_all && (call->a != nullptr && !call->a->empty()) )
        pad = nullptr; // catch_2 takes no arguments — shape mismatch, fail closed
    }
    if ( pad == nullptr )
    {
      const cexpr_t *callee = strip_callee_wrappers(call->x);
      if ( callee == nullptr )
        return false;
      qstring name;
      if ( callee->op == cot_obj )
        get_name(&name, callee->obj_ea);
      else if ( callee->op == cot_helper && callee->helper != nullptr )
        name = callee->helper;
      else
        return false;
      if ( !is_find_matching_catch_name(name.c_str()) )
        return false;
      if ( out_is_catch_all != nullptr )
        *out_is_catch_all
          = is_find_matching_catch_2_name(name.c_str()) && (call->a == nullptr || call->a->empty());
      ++eh_pad_ident_by_name;
    }
    else
    {
      if ( out_is_catch_all != nullptr )
        *out_is_catch_all = pad->is_catch_all;
      ++eh_pad_ident_by_mark;
    }
    if ( out_catch_lvar != nullptr )
      *out_catch_lvar = int(lhs->v.idx);
    return true;
  }

  bool is_resume_call_stmt(const cinsn_t &stmt, int *out_arg_lvar = nullptr)
  {
    const cexpr_t *call = nullptr;
    if ( !stmt_call_of(stmt, &call) )
      return false;
    const bool marked = pad_mark_for_resume(call) != nullptr;
    if ( !marked && !stmt_calls_named(stmt, is_resume_exception_name) )
      return false;
    if ( marked )
      ++eh_pad_ident_by_mark;
    else
      ++eh_pad_ident_by_name;
    if ( call == nullptr || call->a == nullptr || call->a->size() != 1 )
      return false;
    if ( out_arg_lvar != nullptr )
    {
      const cexpr_t *arg0 = strip_cast(&(*call->a)[0]);
      *out_arg_lvar = (arg0 != nullptr && arg0->op == cot_var) ? int(arg0->v.idx) : -1;
    }
    return true;
  }

  bool is_trap_call_stmt(const cinsn_t &stmt)
  {
    if ( is_trap_mark(stmt) )
      return true;
    return stmt_calls_named(stmt, is_builtin_trap_name);
  }

  static bool is_call_or_assign_cleanup_stmt(const cinsn_t &stmt)
  {
    auto check_block = [](const cinsn_t *branch, auto &self) -> bool
    {
      if ( branch == nullptr )
        return true;
      if ( branch->op != cit_block || branch->cblock == nullptr )
        return false;
      for ( const auto &s : *branch->cblock )
        if ( !self(s, self) )
          return false;
      return true;
    };
    auto check_stmt = [&](const cinsn_t &s, auto &self) -> bool
    {
      if ( s.op == cit_empty || s.op == cit_goto )
        return true;
      if ( s.op == cit_expr && s.cexpr != nullptr )
      {
        const cexpr_t *e = strip_cast(s.cexpr);
        return e != nullptr && (e->op == cot_call || is_assignment(e->op));
      }
      if ( s.op == cit_if && s.cif != nullptr )
      {
        return check_block(s.cif->ithen, self) && check_block(s.cif->ielse, self);
      }
      if ( s.op == cit_do && s.cdo != nullptr )
      {
        return check_block(s.cdo->body, self);
      }
      return false;
    };
    return check_stmt(stmt, check_stmt);
  }

  static bool is_threw_read_expr(const cexpr_t *e)
  {
    e = strip_cast(e);
    if ( e == nullptr )
      return false;
    if ( is_threw_obj(e) )
      return true;
    if ( e->op == cot_ptr && e->x != nullptr )
      return is_threw_obj(strip_cast(e->x));
    return false;
  }

  static bool is_threw_cmp_one(const cexpr_t *e, ctype_t expected_op)
  {
    e = strip_cast(e);
    if ( e == nullptr || e->op != expected_op || e->x == nullptr || e->y == nullptr )
      return false;
    const cexpr_t *cx = strip_cast(e->x);
    const cexpr_t *cy = strip_cast(e->y);
    if ( cx == nullptr || cy == nullptr )
      return false;
    if ( cy->op == cot_num && cy->numval() == 1 && is_threw_read_expr(cx) )
      return true;
    if ( cx->op == cot_num && cx->numval() == 1 && is_threw_read_expr(cy) )
      return true;
    return false;
  }

  static bool cond_mentions_threw(const cexpr_t *cond)
  {
    if ( cond == nullptr )
      return false;
    if ( is_threw_cmp_one(cond, cot_eq) || is_threw_cmp_one(cond, cot_ne) )
      return true;
    if ( cond->op == cot_lor || cond->op == cot_land )
      return cond_mentions_threw(cond->x) || cond_mentions_threw(cond->y);
    if ( cond->op == cot_lnot )
      return cond_mentions_threw(cond->x);
    if ( cond->op == cot_comma )
      return cond_mentions_threw(cond->x) || cond_mentions_threw(cond->y);
    return false;
  }

  static bool is_exact_threw_eq_one_cond(const cexpr_t *cond)
  {
    if ( cond == nullptr )
      return false;
    if ( is_threw_cmp_one(cond, cot_eq) )
      return true;
    if ( cond->op == cot_lor )
      return is_exact_threw_eq_one_cond(cond->x) && is_exact_threw_eq_one_cond(cond->y);
    if ( cond->op == cot_comma )
      return is_exact_threw_eq_one_cond(cond->y);
    return false;
  }

  static bool is_call_or_call_assign_stmt(const cinsn_t &stmt)
  {
    if ( stmt.op != cit_expr || stmt.cexpr == nullptr )
      return false;
    const cexpr_t *e = strip_cast(stmt.cexpr);
    if ( e == nullptr )
      return false;
    if ( e->op == cot_call )
      return true;
    if ( e->op == cot_asg && e->y != nullptr )
    {
      const cexpr_t *rhs = strip_cast(e->y);
      return rhs != nullptr && rhs->op == cot_call;
    }
    return false;
  }

  // Locate the guarded call statement immediately preceding `if_it` in
  // `parent_block` (allowing up to 3 intermediate local-to-local copy or
  // constant-flag assignments `v_b = v_a` / `v_flag = 1` produced between the
  // call and its `__THREW__` check).
  static cinsn_t *find_adjacent_guarded_call_stmt(cblock_t *parent_block, cblock_t::iterator if_it)
  {
    if ( parent_block == nullptr || if_it == parent_block->begin() )
      return nullptr;
    auto cur = if_it;
    for ( int step = 0; step < 4 && cur != parent_block->begin(); ++step )
    {
      --cur;
      if ( cur->op == cit_empty )
        continue;
      if ( is_call_or_call_assign_stmt(*cur) )
        return &*cur;
      if ( cur->op == cit_expr && cur->cexpr != nullptr && cur->cexpr->op == cot_asg
           && cur->cexpr->x != nullptr && cur->cexpr->y != nullptr )
      {
        const cexpr_t *lhs = strip_cast(cur->cexpr->x);
        const cexpr_t *rhs = strip_cast(cur->cexpr->y);
        if ( lhs != nullptr && lhs->op == cot_var && rhs != nullptr
             && (rhs->op == cot_var || rhs->op == cot_num) )
          continue;
      }
      return nullptr;
    }
    return nullptr;
  }

  // Check whether `stmt` is one of the two unconditional-throw helpers inside
  // an inlined `std::vector` reallocation block (`__throw_length_error` or
  // `__cxa_throw_bad_array_new_length`) that folds into
  // `std::vector<T>::push_back(&vec, val)`.
  static bool is_vector_realloc_throw_stmt(const cinsn_t &stmt)
  {
    if ( stmt.op != cit_expr || stmt.cexpr == nullptr )
      return false;
    const cexpr_t *call = stmt.cexpr;
    if ( call->op == cot_asg && call->y != nullptr )
      call = call->y;
    call = strip_cast(call);
    if ( call == nullptr || call->op != cot_call || call->x == nullptr )
      return false;
    const cexpr_t *callee = strip_callee_wrappers(call->x);
    qstring name;
    if ( callee != nullptr && callee->op == cot_obj )
      get_name(&name, callee->obj_ea);
    else if ( callee != nullptr && callee->op == cot_helper && callee->helper != nullptr )
      name = callee->helper;
    return name.find("__throw_length_error") != qstring::npos
      || name.find("cxa_throw_bad_array_new_length") != qstring::npos;
  }

  static bool is_vector_realloc_call_stmt(const cinsn_t &stmt, cblock_t *parent_block)
  {
    if ( is_vector_realloc_throw_stmt(stmt) )
      return true;
    if ( stmt.op != cit_expr || stmt.cexpr == nullptr )
      return false;
    const cexpr_t *call = stmt.cexpr;
    if ( call->op == cot_asg && call->y != nullptr )
      call = call->y;
    call = strip_cast(call);
    if ( call == nullptr || call->op != cot_call || call->x == nullptr )
      return false;
    const cexpr_t *callee = strip_callee_wrappers(call->x);
    qstring name;
    if ( callee != nullptr && callee->op == cot_obj )
      get_name(&name, callee->obj_ea);
    else if ( callee != nullptr && callee->op == cot_helper && callee->helper != nullptr )
      name = callee->helper;
    if ( strncmp(name.c_str(), "invoke_", 7) == 0 && call->a != nullptr && call->a->size() >= 2 )
    {
      const cexpr_t *fn_arg = strip_callee_wrappers(&(*call->a)[0]);
      if ( fn_arg != nullptr && fn_arg->op == cot_obj )
      {
        qstring got_name;
        get_name(&got_name, fn_arg->obj_ea);
        if ( got_name.find("_Znwm") != qstring::npos )
        {
          // `std::vector<T>::push_back` realloc blocks always contain both
          // `__throw_length_error` and `__cxa_throw_bad_array_new_length`;
          // standalone `vector::reserve` blocks (like `LABEL_12` in
          // `mergeProcessed`) only contain `__throw_length_error`.
          if ( parent_block != nullptr )
          {
            for ( const auto &s_in : *parent_block )
            {
              if ( is_vector_realloc_throw_stmt(s_in) )
              {
                const cexpr_t *tc = strip_cast(s_in.cexpr);
                if ( tc != nullptr && tc->op == cot_call && tc->x != nullptr )
                {
                  const cexpr_t *tcal = strip_callee_wrappers(tc->x);
                  qstring tname;
                  if ( tcal != nullptr && tcal->op == cot_obj )
                    get_name(&tname, tcal->obj_ea);
                  else if ( tcal != nullptr && tcal->op == cot_helper && tcal->helper != nullptr )
                    tname = tcal->helper;
                  if ( tname.find("cxa_throw_bad_array_new_length") != qstring::npos )
                    return true;
                }
              }
              if ( s_in.op == cit_if && s_in.cif != nullptr && s_in.cif->ithen != nullptr
                   && s_in.cif->ithen->op == cit_block && s_in.cif->ithen->cblock != nullptr )
              {
                for ( const auto &s_sub : *s_in.cif->ithen->cblock )
                {
                  if ( is_vector_realloc_throw_stmt(s_sub) )
                  {
                    const cexpr_t *tc = strip_cast(s_sub.cexpr);
                    if ( tc != nullptr && tc->op == cot_call && tc->x != nullptr )
                    {
                      const cexpr_t *tcal = strip_callee_wrappers(tc->x);
                      qstring tname;
                      if ( tcal != nullptr && tcal->op == cot_obj )
                        get_name(&tname, tcal->obj_ea);
                      else if ( tcal != nullptr && tcal->op == cot_helper
                                && tcal->helper != nullptr )
                        tname = tcal->helper;
                      if ( tname.find("cxa_throw_bad_array_new_length") != qstring::npos )
                        return true;
                    }
                  }
                }
              }
            }
          }
        }
      }
    }
    return false;
  }

  static bool is_cxa_throw_call_stmt(const cinsn_t &stmt)
  {
    if ( stmt.op != cit_expr || stmt.cexpr == nullptr || stmt.cexpr->op != cot_call )
      return false;
    const cexpr_t *call = stmt.cexpr;
    const cexpr_t *callee = strip_callee_wrappers(call->x);
    if ( callee == nullptr )
      return false;
    if ( callee->op == cot_helper && callee->helper != nullptr )
    {
      if ( (strcmp(callee->helper, "_cxa_throw") == 0 || strcmp(callee->helper, "__cxa_throw") == 0)
           && call->a != nullptr && call->a->size() == 3 )
        return true;
      if ( strncmp(callee->helper, "invoke_", 7) == 0 && call->a != nullptr
           && call->a->size() == 4 )
      {
        const cexpr_t *arg0 = strip_callee_wrappers(&(*call->a)[0]);
        if ( arg0 != nullptr && arg0->op == cot_obj )
        {
          qstring name;
          get_name(&name, arg0->obj_ea);
          return name == "_cxa_throw" || name == "__cxa_throw";
        }
      }
    }
    return false;
  }

  static bool is_guard_adjacent_to_call(cblock_t *parent_block, cblock_t::iterator if_it)
  {
    return find_adjacent_guarded_call_stmt(parent_block, if_it) != nullptr;
  }

  bool set_semi_cmt_if_absent(ea_t ea, const char *text)
  {
    if ( ea == BADADDR || text == nullptr )
      return false;
    treeloc_t loc;
    loc.ea = ea;
    loc.itp = ITP_SEMI;
    if ( cfunc->get_user_cmt(loc, RETRIEVE_ALWAYS) != nullptr )
      return false;
    cfunc->set_user_cmt(loc, text);
    return true;
  }

  bool subblock_has_direct_resume(const cinsn_t *branch)
  {
    if ( branch == nullptr || branch->op != cit_block || branch->cblock == nullptr )
      return false;
    for ( const auto &stmt : *branch->cblock )
      if ( stmt_catches_named_or_marked(stmt) )
        return false;
    for ( const auto &stmt : *branch->cblock )
      if ( stmt_resumes_named_or_marked(stmt) )
        return true;
    return false;
  }

  // Structured EH regions — (c1) try-region guard & landing-pad
  // delimiters, plus (c2) provable atomic collapse of empty-cleanup pads and
  // their adjacent invoke guards into `__cxa_rethrow();`.
  eh_detail::eh_region_stats_t eh_stats;

  void structure_eh_regions()
  {
    struct block_and_label_index_t final : public ctree_visitor_t
    {
      std::vector<cblock_t *> blocks;
      std::map<int, std::pair<cblock_t *, cblock_t::iterator>> label_defs;
      std::map<int, size_t> goto_counts;
      std::map<int, std::vector<cinsn_t *>> goto_sites;
      std::map<cblock_t *, cinsn_t *> block_owner_if;
      std::map<cinsn_t *, std::pair<cblock_t *, cblock_t::iterator>> if_location;

      block_and_label_index_t() : ctree_visitor_t(CV_FAST) {}

      int idaapi visit_insn(cinsn_t *insn) override
      {
        if ( insn->op == cit_block && insn->cblock != nullptr )
        {
          cblock_t *b = insn->cblock;
          blocks.push_back(b);
          for ( auto it = b->begin(); it != b->end(); ++it )
          {
            if ( it->label_num >= 0 )
              label_defs[it->label_num] = { b, it };
            if ( it->op == cit_goto && it->cgoto != nullptr )
            {
              ++goto_counts[it->cgoto->label_num];
              goto_sites[it->cgoto->label_num].push_back(&*it);
            }
            if ( it->op == cit_if && it->cif != nullptr )
            {
              if_location[&*it] = { b, it };
              if ( it->cif->ithen != nullptr && it->cif->ithen->op == cit_block
                   && it->cif->ithen->cblock != nullptr )
              {
                block_owner_if[it->cif->ithen->cblock] = &*it;
              }
              if ( it->cif->ielse != nullptr && it->cif->ielse->op == cit_block
                   && it->cif->ielse->cblock != nullptr )
              {
                block_owner_if[it->cif->ielse->cblock] = &*it;
              }
            }
          }
        }
        return 0;
      }
    };

    block_and_label_index_t idx;
    idx.apply_to(&cfunc->body, nullptr);

    struct pad_var_counter_t final : ctree_visitor_t
    {
      std::map<int, size_t> &reads;
      std::map<int, size_t> &writes;
      pad_var_counter_t(std::map<int, size_t> &r, std::map<int, size_t> &w)
          : ctree_visitor_t(CV_FAST | CV_PARENTS), reads(r), writes(w)
      {
      }
      int idaapi visit_expr(cexpr_t *e) override
      {
        if ( e->op == cot_var )
        {
          const int v = int(e->v.idx);
          bool is_write = false;
          if ( !parents.empty() && parents.back() != nullptr && parents.back()->is_expr() )
          {
            const cexpr_t *p = static_cast<const cexpr_t *>(parents.back());
            if ( p->op == cot_asg && strip_cast(p->x) == e )
              is_write = true;
          }
          if ( is_write )
            ++writes[v];
          else
            ++reads[v];
        }
        return 0;
      }
    };

    std::map<int, size_t> current_reads;
    std::map<int, size_t> current_writes;
    pad_var_counter_t count_uses(current_reads, current_writes);
    count_uses.apply_to(&cfunc->body, nullptr);

    // Map each label `L` to the list of exact `if (*__THREW__ == 1) goto L;`
    // guard `cit_if` statements targeting `L`.
    std::map<int, std::vector<cinsn_t *>> label_guard_ifs;
    for ( const auto &kv : idx.if_location )
    {
      cinsn_t *if_insn = kv.first;
      if ( if_insn->cif == nullptr || if_insn->cif->ielse != nullptr
           || if_insn->cif->ithen == nullptr || if_insn->cif->ithen->op != cit_block
           || if_insn->cif->ithen->cblock == nullptr || if_insn->cif->ithen->cblock->size() != 1 )
        continue;
      const cinsn_t &only = if_insn->cif->ithen->cblock->front();
      if ( only.op == cit_goto && only.cgoto != nullptr && only.label_num < 0
           && is_exact_threw_eq_one_cond(&if_insn->cif->expr) )
      {
        label_guard_ifs[only.cgoto->label_num].push_back(if_insn);
      }
    }

    // Helper: trace from `(b, start_it)` through cleanup statements and
    // unconditional `goto` chains to verify a landing pad reaches
    // `__resumeException(v); __builtin_trap();`. When `body_stmts` is not
    // null it records every traced statement, in
    // trace order, including resume/trap and the interior goto statements.
    auto trace_pad_to_resume
      = [&](cblock_t *start_b, cblock_t::iterator catch_it, int catch_lvar,
            eh_detail::eh_landing_pad_candidate_t *cand, std::vector<cinsn_t *> *body_stmts) -> bool
    {
      std::set<int> visited_labels;
      cblock_t *cur_b = start_b;
      auto it = catch_it;
      ++it;
      for ( int hops = 0; hops < 16 && cur_b != nullptr; ++hops )
      {
        for ( ; it != cur_b->end(); ++it )
        {
          if ( stmt_catches_named_or_marked(*it) )
            return false;
          int resume_arg = -1;
          if ( is_resume_call_stmt(*it, &resume_arg) )
          {
            auto trap_it = it;
            ++trap_it;
            cand->ends_with_resume_and_trap
              = (trap_it != cur_b->end() && is_trap_call_stmt(*trap_it));
            cand->resume_uses_catch_var = (catch_lvar >= 0 && resume_arg == catch_lvar);
            if ( cand->ends_with_resume_and_trap && body_stmts != nullptr )
            {
              body_stmts->push_back(&*it);
              body_stmts->push_back(&*trap_it);
            }
            return true;
          }
          if ( it->op == cit_if && it->cif != nullptr
               && (subblock_has_direct_resume(it->cif->ithen)
                   || subblock_has_direct_resume(it->cif->ielse)) )
          {
            ++cand->cleanup_stmt_count;
            cand->ends_with_resume_and_trap = true;
            cand->resume_uses_catch_var = catch_lvar >= 0;
            if ( body_stmts != nullptr )
              body_stmts->push_back(&*it);
            return true;
          }
          if ( it->op == cit_goto && it->cgoto != nullptr )
          {
            const int target_lbl = it->cgoto->label_num;
            if ( !visited_labels.insert(target_lbl).second )
              return false;
            const auto lit = idx.label_defs.find(target_lbl);
            if ( lit == idx.label_defs.end() )
              return false;
            if ( body_stmts != nullptr )
              body_stmts->push_back(&*it);
            cur_b = lit->second.first;
            it = lit->second.second;
            // Re-evaluate starting at the labeled target statement itself.
            if ( stmt_catches_named_or_marked(*it) )
              return false;
            int r_arg = -1;
            if ( is_resume_call_stmt(*it, &r_arg) )
            {
              auto trap_it = it;
              ++trap_it;
              cand->ends_with_resume_and_trap
                = (trap_it != cur_b->end() && is_trap_call_stmt(*trap_it));
              cand->resume_uses_catch_var = (catch_lvar >= 0 && r_arg == catch_lvar);
              if ( cand->ends_with_resume_and_trap && body_stmts != nullptr )
              {
                body_stmts->push_back(&*it);
                body_stmts->push_back(&*trap_it);
              }
              return true;
            }
            if ( it->op == cit_if && it->cif != nullptr
                 && (subblock_has_direct_resume(it->cif->ithen)
                     || subblock_has_direct_resume(it->cif->ielse)) )
            {
              ++cand->cleanup_stmt_count;
              cand->ends_with_resume_and_trap = true;
              cand->resume_uses_catch_var = catch_lvar >= 0;
              if ( body_stmts != nullptr )
                body_stmts->push_back(&*it);
              return true;
            }
            if ( !is_call_or_assign_cleanup_stmt(*it) )
              cand->all_cleanup_stmts_calls_or_assigns = false;
            ++cand->cleanup_stmt_count;
            if ( body_stmts != nullptr )
              body_stmts->push_back(&*it);
            continue;
          }
          if ( !is_call_or_assign_cleanup_stmt(*it) )
            cand->all_cleanup_stmts_calls_or_assigns = false;
          ++cand->cleanup_stmt_count;
          if ( body_stmts != nullptr )
            body_stmts->push_back(&*it);
        }
        break;
      }
      return false;
    };

    std::set<cinsn_t *> collapsed_guard_ifs;
    std::set<cinsn_t *> pad_stmts_to_remove;
    struct deferred_pad_collapse_t
    {
      cinsn_t *catch_stmt;
      cinsn_t *s1_stmt;
      cinsn_t *s2_stmt;
      int catch_lvar;
      std::vector<std::pair<cinsn_t *, cexpr_t *>> replacements;
    };
    std::vector<deferred_pad_collapse_t> deferred_collapses;

    // Every valid landing pad with its traced body statements, for
    // the whole-function move analysis that runs after all pads are known.
    struct pad_record_t
    {
      cinsn_t *catch_stmt;
      std::vector<cinsn_t *> body_stmts;
      eh_detail::eh_landing_pad_candidate_t cand;
    };
    std::vector<pad_record_t> pad_records;

    for ( cblock_t *b : idx.blocks )
    {
      for ( auto it = b->begin(); it != b->end(); ++it )
      {
        int catch_lvar = -1;
        bool is_catch_all = false;
        if ( !is_catch_assign_stmt(*it, &catch_lvar, &is_catch_all) )
          continue;

        eh_detail::eh_landing_pad_candidate_t cand;
        cand.starts_with_find_matching_catch = true;
        cand.is_catch_all = is_catch_all;

        std::vector<cinsn_t *> pad_body_stmts;
        if ( !trace_pad_to_resume(b, it, catch_lvar, &cand, &pad_body_stmts)
             || !eh_detail::is_valid_eh_landing_pad(cand) )
          continue;

        // Check incoming predecessor edges to this pad block `b`.
        std::vector<cinsn_t *> pad_guards;
        bool PredecessorsValid = true;
        const auto owner_it = idx.block_owner_if.find(b);
        const bool owner_exists = owner_it != idx.block_owner_if.end();
        if ( it == b->begin() && owner_exists )
        {
          cinsn_t *owner_if = owner_it->second;
          if ( owner_if->label_num < 0 && owner_if->cif != nullptr
               && owner_if->cif->ielse == nullptr && owner_if->cif->ithen != nullptr
               && owner_if->cif->ithen->cblock == b
               && is_exact_threw_eq_one_cond(&owner_if->cif->expr) )
          {
            pad_guards.push_back(owner_if);
            cand.entry_owned_by_guard = true;
          }
          else
          {
            PredecessorsValid = false;
            ++cand.non_guard_predecessor_count;
            cand.pred_blocker = std::uint8_t(eh_detail::eh_pred_blocker_t::owner_guard_not_exact);
          }
        }
        else if ( it != b->begin() || owner_exists )
        {
          // Catch is not the first statement of its block (a same-block
          // statement can fall into it) — predecessor edges unproven.
          PredecessorsValid = false;
          ++cand.non_guard_predecessor_count;
          cand.pred_blocker = std::uint8_t(eh_detail::eh_pred_blocker_t::catch_not_block_first);
        }
        else
        {
          // Plain label-tail block with no owning guard: fall-in through
          // the preceding sibling of this block is not disproven.
          // tracks this population separately; edges stay unproven.
          PredecessorsValid = false;
          ++cand.non_guard_predecessor_count;
          ++eh_stats.pads_label_tail_entry;
          cand.pred_blocker = std::uint8_t(eh_detail::eh_pred_blocker_t::plain_block_fallin);
        }

        // Check any label on the catch statement (or inside the empty pad).
        if ( it->label_num >= 0 )
        {
          const int lbl = it->label_num;
          const size_t total_gotos = idx.goto_counts[lbl];
          const auto git = label_guard_ifs.find(lbl);
          const size_t guard_gotos = git == label_guard_ifs.end() ? 0 : git->second.size();
          if ( total_gotos > guard_gotos )
          {
            PredecessorsValid = false;
            cand.non_guard_predecessor_count += (total_gotos - guard_gotos);
            if ( cand.pred_blocker == std::uint8_t(eh_detail::eh_pred_blocker_t::none) )
              cand.pred_blocker = std::uint8_t(eh_detail::eh_pred_blocker_t::non_guard_gotos);
          }
          if ( git != label_guard_ifs.end() )
          {
            for ( cinsn_t *g_if : git->second )
              pad_guards.push_back(g_if);
          }
        }

        // For an empty pad (`b->size() == 3`), check that statements 1 and 2
        // carry no labels of their own.
        if ( cand.cleanup_stmt_count == 0 && b->size() == 3 )
        {
          auto s1 = it;
          ++s1;
          auto s2 = s1;
          ++s2;
          if ( s1->label_num >= 0 || s2->label_num >= 0 )
          {
            PredecessorsValid = false;
            ++cand.non_guard_predecessor_count;
          }
        }

        cand.guard_predecessor_count = pad_guards.size();
        if ( PredecessorsValid && !pad_guards.empty() )
        {
          bool all_adj = true;
          for ( cinsn_t *g_if : pad_guards )
          {
            const auto loc_it = idx.if_location.find(g_if);
            if ( loc_it == idx.if_location.end()
                 || !is_guard_adjacent_to_call(loc_it->second.first, loc_it->second.second) )
            {
              all_adj = false;
              break;
            }
          }
          cand.all_guards_adjacent_to_invoke = all_adj;
          if ( !all_adj )
            cand.pred_blocker = std::uint8_t(eh_detail::eh_pred_blocker_t::nonadj_guard);
        }

        // In an empty pad (`v_exc = __cxa_find_matching_catch_2();
        // __resumeException(v_exc); __builtin_trap();`), `v_exc` has 1 write
        // and 1 read (inside `__resumeException(v_exc)`).
        const size_t total_catch_reads = catch_lvar >= 0 ? current_reads[catch_lvar] : 0;
        const size_t total_catch_writes = catch_lvar >= 0 ? current_writes[catch_lvar] : 0;
        cand.catch_temp_does_not_escape
          = (catch_lvar >= 0 && total_catch_reads == 1 && total_catch_writes == 1);

        const eh_detail::eh_pad_skip_reason_t reason = eh_detail::classify_eh_pad_collapse(cand);

        // Record every valid pad (with its traced body) for the
        // post-scan move analysis. Recorded before the c2 collapse branch so
        // collapsed empty pads appear as not_candidate plans.
        pad_records.push_back({ &*it, std::move(pad_body_stmts), cand });

        if ( reason == eh_detail::eh_pad_skip_reason_t::none && b->size() == 3 )
        {
          // Atomically collapse the empty-cleanup landing pad and all
          // of its incoming guard `if` sites into `if (*__THREW__ == 1) __cxa_rethrow();`.
          bool all_built = true;
          std::vector<std::pair<cinsn_t *, cexpr_t *>> replacements;
          for ( cinsn_t *g_if : pad_guards )
          {
            const ea_t ea = g_if->ea != BADADDR ? g_if->ea : it->ea;
            cexpr_t *rethrow_call
              = call_helper(tinfo_t(BT_VOID), new carglist_t, "%s", "__cxa_rethrow");
            if ( rethrow_call == nullptr )
            {
              all_built = false;
              break;
            }
            rethrow_call->ea = ea;
            replacements.push_back({ g_if, rethrow_call });
          }
          if ( all_built )
          {
            auto s1 = it;
            ++s1;
            auto s2 = s1;
            ++s2;
            for ( const auto &rep : replacements )
            {
              collapsed_guard_ifs.insert(rep.first);
              ++eh_stats.guards_collapsed;
              ++eh_stats.guards_total;
            }
            deferred_collapses.push_back({ &*it, &*s1, &*s2, catch_lvar, std::move(replacements) });
            ++eh_stats.pads_collapsed;
            break;
          }
          for ( const auto &rep : replacements )
            delete rep.second;
        }

        if ( reason != eh_detail::eh_pad_skip_reason_t::none )
          ++eh_stats.pads_skipped_by_reason[std::size_t(reason)];

        if ( owner_it != idx.block_owner_if.end() )
        {
          const auto oloc = idx.if_location.find(owner_it->second);
          if ( oloc != idx.if_location.end() )
          {
            cinsn_t *owner_call
              = find_adjacent_guarded_call_stmt(oloc->second.first, oloc->second.second);
            if ( owner_call != nullptr && is_vector_realloc_throw_stmt(*owner_call) )
              continue;
          }
        }
        if ( it->label_num >= 0 )
        {
          bool jumped_from_realloc_throw = false;
          for ( cblock_t *b2 : idx.blocks )
          {
            if ( b2 == b )
            {
              for ( const auto &s_in : *b2 )
              {
                if ( is_vector_realloc_throw_stmt(s_in) && idx.goto_counts[it->label_num] <= 2 )
                {
                  jumped_from_realloc_throw = true;
                  break;
                }
              }
            }
          }
          if ( jumped_from_realloc_throw && idx.goto_counts[it->label_num] <= 2 )
            continue;
        }

        ea_t cmt_ea = it->ea;
        if ( cmt_ea == BADADDR )
        {
          for ( const auto &stmt : *b )
          {
            if ( stmt.ea != BADADDR )
            {
              cmt_ea = stmt.ea;
              break;
            }
          }
        }
        if ( set_semi_cmt_if_absent(cmt_ea, "wasmdec: EH landing pad (catch + cleanup + resume)") )
          ++eh_stats.pads_annotated;
      }
    }

    for ( const auto &kv : idx.if_location )
    {
      cinsn_t *if_insn = kv.first;
      if ( collapsed_guard_ifs.count(if_insn) != 0 )
        continue;
      if ( if_insn->cif == nullptr || !cond_mentions_threw(&if_insn->cif->expr) )
        continue;
      ++eh_stats.guards_total;
      cinsn_t *call_stmt = find_adjacent_guarded_call_stmt(kv.second.first, kv.second.second);
      if ( call_stmt == nullptr || is_vector_realloc_call_stmt(*call_stmt, kv.second.first)
           || is_cxa_throw_call_stmt(*call_stmt) )
        continue;
      const ea_t call_ea = call_stmt->ea != BADADDR
        ? call_stmt->ea
        : (call_stmt->cexpr != nullptr ? call_stmt->cexpr->ea : BADADDR);
      if ( set_semi_cmt_if_absent(call_ea, "wasmdec: try-region") )
        ++eh_stats.guards_delimited;
    }

    // Whole-function move analysis for non-empty pads. Pure
    // analysis — fills shared_body_stmts / exterior_label_jumps on every
    // recorded pad and classifies the structuring plan per pad. A pad body
    // may only move into its EA-first guard's branch when no other pad
    // traces the same statements (shared destructor ladders) and no goto
    // from outside the body targets an interior label of the body.
    {
      std::map<cinsn_t *, std::size_t> stmt_pad_refs;
      for ( const auto &p : pad_records )
        for ( cinsn_t *s : p.body_stmts )
          ++stmt_pad_refs[s];
      for ( auto &p : pad_records )
      {
        std::set<cinsn_t *> own;
        own.insert(p.catch_stmt);
        for ( cinsn_t *s : p.body_stmts )
        {
          own.insert(s);
          if ( stmt_pad_refs[s] > 1 )
            ++p.cand.shared_body_stmts;
        }
        for ( cinsn_t *s : p.body_stmts )
        {
          if ( s->label_num < 0 )
            continue;
          const auto sit = idx.goto_sites.find(s->label_num);
          if ( sit == idx.goto_sites.end() )
            continue;
          for ( cinsn_t *site : sit->second )
            if ( own.count(site) == 0 )
              ++p.cand.exterior_label_jumps;
        }
        const eh_detail::eh_pad_move_plan_t plan = eh_detail::classify_eh_pad_move(p.cand);
        ++eh_stats.pads_by_move_plan[std::size_t(plan)];
        if ( plan == eh_detail::eh_pad_move_plan_t::unproven_pred_edge
             && p.cand.pred_blocker < std::size_t(eh_detail::eh_pred_blocker_t::count) )
        {
          ++eh_stats.pred_blockers[p.cand.pred_blocker];
        }
      }
    }

    // Apply all deferred c2 collapses now that pad analysis and c1 guard
    // scanning are complete.
    for ( const auto &dc : deferred_collapses )
    {
      dc.catch_stmt->label_num = -1;
      dc.s1_stmt->label_num = -1;
      dc.s2_stmt->label_num = -1;
      for ( const auto &rep : dc.replacements )
      {
        cinsn_t *g_if = rep.first;
        cexpr_t *rethrow_call = rep.second;
        cinsn_t &first_stmt = g_if->cif->ithen->cblock->front();
        const ea_t stmt_ea = first_stmt.ea != BADADDR ? first_stmt.ea : rethrow_call->ea;
        rethrow_call->ea = stmt_ea;
        if ( first_stmt.op == cit_expr && first_stmt.cexpr != nullptr )
        {
          first_stmt.cexpr->replace_by(rethrow_call);
        }
        else
        {
          first_stmt.cleanup();
          first_stmt.op = cit_expr;
          first_stmt.cexpr = rethrow_call;
        }
        first_stmt.ea = stmt_ea;
        first_stmt.label_num = -1;
        set_semi_cmt_if_absent(stmt_ea,
                               "wasmdec: try-region rethrow (empty landing pad collapsed)");
      }
      if ( dc.catch_lvar >= 0 && cfunc->get_lvars() != nullptr
           && size_t(dc.catch_lvar) < cfunc->get_lvars()->size() )
      {
        (*cfunc->get_lvars())[size_t(dc.catch_lvar)].clear_used();
      }
      pad_stmts_to_remove.insert(dc.s1_stmt);
      pad_stmts_to_remove.insert(dc.s2_stmt);
    }

    if ( !pad_stmts_to_remove.empty() )
      remove_block_statements(cfunc, pad_stmts_to_remove);
    if ( collapsed_guard_ifs.size() > 1 )
      cfunc->remove_unused_labels();

    if ( options::debug("eh") || options::debug("cpp") )
    {
      msg(
        "WASMHEX_EH_STATS entry=%a guards_total=%zu guards_delimited=%zu "
        "guards_collapsed=%zu pads_annotated=%zu pads_collapsed=%zu pads_skipped=%zu "
        "pad_ident[mark=%zu name=%zu] "
        "[catch=%zu resume=%zu cleanup_kind=%zu non_empty=%zu pred=%zu adj=%zu escape=%zu]\n",
        cfunc->entry_ea, eh_stats.guards_total, eh_stats.guards_delimited,
        eh_stats.guards_collapsed, eh_stats.pads_annotated, eh_stats.pads_collapsed,
        eh_stats.total_pads_skipped(), eh_pad_ident_by_mark, eh_pad_ident_by_name,
        eh_stats
          .pads_skipped_by_reason[std::size_t(eh_detail::eh_pad_skip_reason_t::missing_catch_call)],
        eh_stats.pads_skipped_by_reason[std::size_t(
          eh_detail::eh_pad_skip_reason_t::missing_resume_or_trap)],
        eh_stats.pads_skipped_by_reason[std::size_t(
          eh_detail::eh_pad_skip_reason_t::non_call_or_assign_cleanup)],
        eh_stats
          .pads_skipped_by_reason[std::size_t(eh_detail::eh_pad_skip_reason_t::non_empty_cleanup)],
        eh_stats.pads_skipped_by_reason[std::size_t(
          eh_detail::eh_pad_skip_reason_t::unproven_predecessor_edge)],
        eh_stats.pads_skipped_by_reason[std::size_t(
          eh_detail::eh_pad_skip_reason_t::non_adjacent_invoke_guard)],
        eh_stats
          .pads_skipped_by_reason[std::size_t(eh_detail::eh_pad_skip_reason_t::escaping_pad_temp)]);
      // Corpus-survey aid (MC_DUMP precedent in ctree.cpp): the
      // per-function move-plan line goes to stderr unbuffered so batch
      // surveys can collect it without a ui_msg listener.
      {
        char mv[512];
        const int n = qsnprintf(
          mv, sizeof(mv),
          "WASMHEX_EH_MOVE entry=%a plans[not_candidate=%zu already_inline=%zu "
          "unproven_pred=%zu shared_tail=%zu exterior_jump=%zu movable=%zu] "
          "label_tail_entry=%zu blockers[owner_nonexact=%zu not_first=%zu fallin=%zu "
          "gotos=%zu nonadj=%zu]\n",
          cfunc->entry_ea,
          eh_stats.pads_by_move_plan[std::size_t(eh_detail::eh_pad_move_plan_t::not_candidate)],
          eh_stats.pads_by_move_plan[std::size_t(eh_detail::eh_pad_move_plan_t::already_inline)],
          eh_stats
            .pads_by_move_plan[std::size_t(eh_detail::eh_pad_move_plan_t::unproven_pred_edge)],
          eh_stats
            .pads_by_move_plan[std::size_t(eh_detail::eh_pad_move_plan_t::shared_cleanup_tail)],
          eh_stats.pads_by_move_plan[std::size_t(eh_detail::eh_pad_move_plan_t::exterior_jump_in)],
          eh_stats.pads_by_move_plan[std::size_t(eh_detail::eh_pad_move_plan_t::movable)],
          eh_stats.pads_label_tail_entry,
          eh_stats.pred_blockers[std::size_t(eh_detail::eh_pred_blocker_t::owner_guard_not_exact)],
          eh_stats.pred_blockers[std::size_t(eh_detail::eh_pred_blocker_t::catch_not_block_first)],
          eh_stats.pred_blockers[std::size_t(eh_detail::eh_pred_blocker_t::plain_block_fallin)],
          eh_stats.pred_blockers[std::size_t(eh_detail::eh_pred_blocker_t::non_guard_gotos)],
          eh_stats.pred_blockers[std::size_t(eh_detail::eh_pred_blocker_t::nonadj_guard)]);
        if ( n > 0 )
          ::write(2, mv, size_t(n));
      }
    }
  }

public:
  explicit wasmhex_eh_rewriter_t(cfunc_t *_cfunc) : ctree_visitor_t(CV_POST), cfunc(_cfunc) {}

  int idaapi leave_expr(cexpr_t *expression) override
  {
    fix_function_pointer(expression);
    return 0;
  }

  int idaapi visit_insn(cinsn_t *insn) override
  {
    if ( insn->op != cit_block || insn->cblock == nullptr )
      return 0;

    cblock_t *block = insn->cblock;

    for ( auto it = block->begin(); it != block->end(); ++it )
    {
      if ( !is_invoke_call(*it) )
        continue;
      // Performance: removal decisions come from whole-function use counts
      // (func_total_uses) plus the uses inside removed statements — O(1)
      // per decision. Re-scanning following statements per invoke was both
      // quadratic (it hung for minutes on the large EH block at duckdb
      // 0x110AA4) and block-scoped (uses in sibling blocks went unseen and
      // definitions were dropped while still read).
      auto schedule_removal = [&](cinsn_t *stmt)
      {
        if ( stmt == nullptr || !to_remove.insert(stmt).second )
          return; // null or already slated for removal
        std::map<int, size_t> stmt_counts;
        use_counter_t counter(stmt_counts);
        counter.apply_to(stmt, nullptr);
        for ( const auto &kv : stmt_counts )
          removal_uses[kv.first] += kv.second;
      };

      // Provenance pass: find the LAST `v = <__THREW__>` assignment
      // before the call; invalidate if v is reassigned between that
      // assignment and the call. Without provenance, `*(v) = 0` stores of
      // unrelated locals are NOT EH protocol stores and stay untouched.
      int threw_ptr_lvar = -1;
      cinsn_t *ptr_assign_stmt = nullptr;
      {
        int candidate = -1;
        cinsn_t *candidate_stmt = nullptr;
        for ( auto bit = block->begin(); bit != it; ++bit )
        {
          if ( bit->op != cit_expr || bit->cexpr == nullptr )
            continue;
          cexpr_t *expr = bit->cexpr;
          if ( expr->op != cot_asg || expr->x == nullptr )
            continue;
          const cexpr_t *lhs = strip_cast(expr->x);
          if ( lhs->op != cot_var )
            continue;
          if ( is_threw_obj(expr->y) )
          {
            candidate = lhs->v.idx;
            candidate_stmt = &*bit;
          }
          else if ( lhs->v.idx == candidate && candidate >= 0 )
          {
            candidate = -1;
            candidate_stmt = nullptr;
          }
        }
        threw_ptr_lvar = candidate;
        ptr_assign_stmt = candidate_stmt;
      }

      // zero_before: the LAST store before the call binding the flag.
      cinsn_t *zero_before_stmt = nullptr;
      for ( auto bit = block->begin(); bit != it; ++bit )
      {
        if ( bit->op != cit_expr || bit->cexpr == nullptr )
          continue;
        if ( is_zero_store_of(bit->cexpr, threw_ptr_lvar) )
          zero_before_stmt = &*bit;
      }
      if ( zero_before_stmt == nullptr )
      {
        continue;
      }

      // With the EH protocol proven, rewrite the shim call itself.
      try_direct_call(*it);

      int read_lvar = -1;
      cinsn_t *read_stmt = nullptr;
      cinsn_t *zero_after_stmt = nullptr;
      cinsn_t *if_stmt = nullptr;
      const cexpr_t *threw_expr = nullptr;

      for ( auto fit = it; ++fit != block->end(); )
      {
        cinsn_t &curr = *fit;
        if ( curr.op == cit_expr && curr.cexpr != nullptr && curr.cexpr->op == cot_asg )
        {
          cexpr_t *expr = curr.cexpr;
          if ( is_zero_store_of(expr, threw_ptr_lvar) )
          {
            zero_after_stmt = &curr;
          }
          else if ( expr->x != nullptr && expr->x->op == cot_var )
          {
            const cexpr_t *rhs = strip_cast(expr->y);
            if ( rhs == nullptr )
              continue;
            if ( is_threw_obj(rhs) )
            {
              read_lvar = expr->x->v.idx;
              read_stmt = &curr;
              if ( threw_expr == nullptr )
                threw_expr = rhs;
            }
            else if ( rhs->op == cot_ptr && threw_ptr_lvar >= 0 )
            {
              const cexpr_t *base = strip_cast(rhs->x);
              if ( base != nullptr && base->op == cot_var && base->v.idx == threw_ptr_lvar )
              {
                read_lvar = expr->x->v.idx;
                read_stmt = &curr;
                if ( threw_expr == nullptr )
                  threw_expr = rhs;
              }
            }
          }
        }
        else if ( curr.op == cit_if && curr.cif != nullptr )
        {
          if_stmt = &curr;
          break;
        }
        else if ( curr.op == cit_goto )
        {
          break;
        }
      }

      schedule_removal(zero_before_stmt);
      schedule_removal(zero_after_stmt);

      if ( read_stmt != nullptr && if_stmt != nullptr && read_lvar >= 0 && threw_expr != nullptr )
      {
        const cexpr_t *ptr_origin
          = (ptr_assign_stmt != nullptr && ptr_assign_stmt->cexpr != nullptr)
          ? ptr_assign_stmt->cexpr->y
          : nullptr;
        cexpr_t *read_clone = build_threw_read_clone(threw_expr, ptr_origin, threw_ptr_lvar);
        const size_t replaced
          = replace_threw_read_in_cond(&if_stmt->cif->expr, read_lvar, *read_clone);
        delete read_clone;
        if ( replaced > 0 )
        {
          if ( all_uses_vanish(read_lvar, replaced) )
            schedule_removal(read_stmt);
          if ( ptr_assign_stmt != nullptr && all_uses_vanish(threw_ptr_lvar, 0) )
            schedule_removal(ptr_assign_stmt);
        }
      }
    }

    // Clean comma-chain invoke protocols inside `if` conditions
    // (such as the RHS of `||` / `&&` in `AddRelationTdom` and
    // `ExtractJoinRelations`).
    for ( auto it = block->begin(); it != block->end(); ++it )
    {
      if ( it->op == cit_if && it->cif != nullptr )
        clean_if_condition(&it->cif->expr);
    }

    return 0;
  }

  void apply()
  {
    // Load the EH marks (blob 'i') first — invoke identity below
    // reads them. The pass still runs unmarked functions through the region
    // structuring analysis (labels/gotos need no invoke evidence).
    load_eh_marks();
    // Function-wide use counts before any rewriting — removal decisions
    // must see uses outside the current block (lvar scope is the function).
    use_counter_t totals(func_total_uses);
    totals.apply_to(&cfunc->body, nullptr);
    apply_to(&cfunc->body, nullptr);
    remove_block_statements(cfunc, to_remove);
    structure_eh_regions();
  }
};

// Entry point used by the CMAT_FINAL pipeline in ctree.cpp.
void wasmhex_apply_eh_rewriter(cfunc_t *cfunc)
{
  wasmhex_eh_rewriter_t rewriter(cfunc);
  rewriter.apply();
}

} // namespace wasmhex

#endif // WASMHEX_PURE_STRING_TEST
