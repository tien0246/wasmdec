#include "../../../core/toolchains/go/ir.hpp"

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace wasmhex
{
namespace go_wb_detail
{

// Fail-closed skip reasons for consuming a proven write-barrier mark
// at the ctree layer. These are PRESENTATION guards (removability of the
// structured `if`, adjacency of the post-barrier store) — the protocol
// itself was already proven in the loader IR.
enum class wb_skip_reason_t : std::uint8_t
{
  none = 0,
  has_else,
  inner_label,
  stale_mark,
  call_not_in_if,
  missing_post_store,
  count
};

inline const char *wb_skip_reason_name(wb_skip_reason_t reason)
{
  switch ( reason )
  {
    case wb_skip_reason_t::none: return "none";
    case wb_skip_reason_t::has_else: return "has_else";
    case wb_skip_reason_t::inner_label: return "inner_label";
    case wb_skip_reason_t::stale_mark: return "stale_mark";
    case wb_skip_reason_t::call_not_in_if: return "call_not_in_if";
    case wb_skip_reason_t::missing_post_store: return "missing_post_store";
    case wb_skip_reason_t::count: break;
  }
  return "unknown";
}

struct wb_match_stats_t
{
  std::size_t matched = 0;
  std::size_t skipped_by_reason[std::size_t(wb_skip_reason_t::count)] = {};

  std::size_t total_skipped() const
  {
    std::size_t sum = 0;
    for ( std::size_t i = 1; i < std::size_t(wb_skip_reason_t::count); ++i )
      sum += skipped_by_reason[i];
    return sum;
  }
};

} // namespace go_wb_detail
} // namespace wasmhex

#ifndef WASMHEX_PURE_STRING_TEST

#include "ctree.hpp"

#include "../../../ida/loader/options.hpp"

#include <bytes.hpp>
#include <name.hpp>
#include <segment.hpp>
#include <typeinf.hpp>

#include <map>
#include <set>
#include <vector>

#include "../../../backend/idb/metadata_schema/metadata.hpp"
#include "ctree_types.hpp"

namespace wasmhex
{

extern "C" __attribute__((used)) const char wasmhex_g6_wb_build_marker[]
  = "WASMHEX_BUILD_MARKER_M4_D4_WB_IR_MARKS";

namespace
{

// The Go rewriter. Every recognition decision comes from the loader IR
// (blob 'x' GOMS + GOWB tails); this layer only presents.
class wasmhex_go_rewriter_t final : public ctree_visitor_t
{
  cfunc_t *cfunc;
  std::set<cinsn_t *> to_remove;
  std::set<int> wb_removed_vars;
  std::map<int, std::size_t> func_var_refs;
  go_wb_detail::wb_match_stats_t wb_stats;
  // Proven write-barrier marks for THIS function, keyed by the guard
  // `if` EA (entry + guard_if_offset), loaded lazily with the rest of the
  // Go IR.
  loader::go_function_ir_t go_ir;
  bool go_ir_loaded = false;

  struct var_ref_counter_t final : public ctree_visitor_t
  {
    std::map<int, std::size_t> &counts;
    explicit var_ref_counter_t(std::map<int, std::size_t> &c) : ctree_visitor_t(CV_FAST), counts(c)
    {
    }

    int idaapi visit_expr(cexpr_t *e) override
    {
      if ( e->op == cot_var )
        ++counts[int(e->v.idx)];
      return 0;
    }
  };

  void schedule_wb_removal(cinsn_t *stmt)
  {
    if ( stmt == nullptr || !to_remove.insert(stmt).second )
      return;
    std::map<int, std::size_t> local_refs;
    var_ref_counter_t counter(local_refs);
    counter.apply_to(stmt, nullptr);
    for ( const auto &kv : local_refs )
      wb_removed_vars.insert(kv.first);
  }

  static const cexpr_t *strip_cast(const cexpr_t *e)
  {
    while ( e != nullptr && e->op == cot_cast )
      e = e->x;
    return e;
  }

  static const cexpr_t *strip_casts_and_lodword(const cexpr_t *e)
  {
    while ( e != nullptr )
    {
      if ( e->op == cot_cast && e->x != nullptr )
      {
        e = e->x;
        continue;
      }
      if ( e->op == cot_call && e->x != nullptr && e->x->op == cot_helper && e->x->helper != nullptr
           && std::strcmp(e->x->helper, "LODWORD") == 0 && e->a != nullptr && e->a->size() == 1 )
      {
        e = &(*e->a)[0];
        continue;
      }
      break;
    }
    return e;
  }

  // Canonical EAs of the Go ABI globals from the loader metadata (0 = SP,
  // 2 = g). BADADDR when the module carries no such global.
  static ea_t global_object_address(uint32 index)
  {
    uchar type = 0, mutability = 0;
    ea_t address = BADADDR;
    bool imported = false;
    qstring helper;
    if ( !get_global_metadata(index, &type, &mutability, &address, &imported, &helper) )
      return BADADDR;
    return imported || address == BADADDR ? ea_t(BADADDR) : address;
  }

  static bool is_linear_memory_object(ea_t ea)
  {
    return ea != BADADDR && uint64(ea) >= wasmhex_memory_base
      && uint64(ea) < wasmhex_memory_base + 0x100000000ULL;
  }

  static bool is_go_runtime_global_ea(ea_t ea)
  {
    if ( ea == BADADDR )
      return false;
    for ( uint32 idx = 0; idx < 16; ++idx )
      if ( global_object_address(idx) == ea )
        return true;
    return false;
  }

  // The Go IR (blob 'x') for the function being rewritten, loaded once.
  // Absent (nullptr) for non-Go modules and legacy blobs.
  const loader::go_function_ir_t *current_go_ir()
  {
    if ( !go_ir_loaded )
    {
      go_ir_loaded = true;
      netnode wasm_node(wasmhex_node_name, 0, false);
      if ( wasm_node != BADNODE )
        load_go_function_ir(wasm_node, cfunc->entry_ea, &go_ir, nullptr);
    }
    return go_ir.present ? &go_ir : nullptr;
  }

  // The morestack stack-check prologue is proven in the Go IR. This
  // ctree layer only consumes the mark: the `if` at the proven EA
  // goes, whatever Hex-Rays did to the body legs. Defense against a stale
  // blob: the EA must still decode as an `if`.
  bool prologue_mark_matches(const cinsn_t *insn)
  {
    if ( insn == nullptr || insn->op != cit_if || insn->cif == nullptr
         || insn->cif->ithen == nullptr || insn->cif->ielse != nullptr )
    {
      return false;
    }
    const loader::go_function_ir_t *ir = current_go_ir();
    if ( ir == nullptr || ir->prologue.if_offset == UINT32_MAX )
      return false;
    const ea_t if_ea = cfunc->entry_ea + ir->prologue.if_offset;
    if ( insn->ea != if_ea )
      return false;
    insn_t decoded;
    return decode_insn(&decoded, if_ea) > 0 && decoded.itype == wasmhex_if;
  }

  // Write-barrier mark consumption

  static bool expr_has_side_effects(const cexpr_t *e)
  {
    if ( e == nullptr )
      return false;
    if ( is_assignment(e->op) || e->op == cot_postinc || e->op == cot_postdec || e->op == cot_preinc
         || e->op == cot_predec )
      return true;
    if ( e->op == cot_call )
    {
      if ( e->x != nullptr && e->x->op == cot_helper && e->x->helper != nullptr
           && (std::strcmp(e->x->helper, "LODWORD") == 0
               || std::strcmp(e->x->helper, "HIDWORD") == 0
               || std::strcmp(e->x->helper, "LOWORD") == 0
               || std::strcmp(e->x->helper, "LOBYTE") == 0)
           && e->a != nullptr && e->a->size() == 1 )
      {
        return expr_has_side_effects(&(*e->a)[0]);
      }
      return true;
    }
    if ( op_uses_x(e->op) && expr_has_side_effects(e->x) )
      return true;
    if ( op_uses_y(e->op) && expr_has_side_effects(e->y) )
      return true;
    if ( op_uses_z(e->op) && expr_has_side_effects(e->z) )
      return true;
    return false;
  }

  // A static 4-byte memory read containing no local variables and no Go
  // runtime globals — the shape of the hoisted `runtime.writeBarrier.enabled`
  // flag definition `v_flag = (writeBarrier.enabled == 0)`.
  static bool is_static_wb_flag_read(const cexpr_t *e)
  {
    e = strip_casts_and_lodword(e);
    if ( e == nullptr || expr_has_side_effects(e) )
      return false;
    if ( e->op == cot_obj )
      return !is_go_runtime_global_ea(e->obj_ea);
    if ( e->op != cot_ptr && e->op != cot_idx )
      return false;
    struct flatten_t final : ctree_visitor_t
    {
      qvector<const cexpr_t *> *parts;
      flatten_t(qvector<const cexpr_t *> *p) : ctree_visitor_t(CV_FAST), parts(p) {}
      int idaapi visit_expr(cexpr_t *x) override
      {
        if ( x->op == cot_add || x->op == cot_ref || x->op == cot_idx )
          return 0; // flatten through
        parts->push_back(x);
        return 0;
      }
    };
    qvector<const cexpr_t *> parts;
    flatten_t flattener(&parts);
    flattener.apply_to(const_cast<cexpr_t *>(e->op == cot_ptr ? e->x : e), nullptr);
    if ( parts.empty() )
      return false;
    bool saw_address = false;
    for ( const cexpr_t *part : parts )
    {
      part = strip_cast(part);
      if ( part == nullptr )
        return false;
      if ( part->op == cot_num )
      {
        saw_address = true;
      }
      else if ( part->op == cot_obj )
      {
        if ( is_go_runtime_global_ea(part->obj_ea) )
          return false;
        saw_address = true;
      }
      else
      {
        return false; // forbids cot_var and any dynamic address
      }
    }
    return saw_address;
  }

  // `v == 0` / `v != 0` over the static flag read.
  static bool is_wb_flag_cmp_zero(const cexpr_t *e, ctype_t expected_cmp)
  {
    e = strip_cast(e);
    if ( e == nullptr || e->op != expected_cmp || e->x == nullptr || e->y == nullptr )
      return false;
    const cexpr_t *lhs = strip_cast(e->x);
    const cexpr_t *rhs = strip_cast(e->y);
    if ( rhs != nullptr && rhs->op == cot_num && rhs->numval() == 0
         && is_static_wb_flag_read(e->x) )
      return true;
    if ( lhs != nullptr && lhs->op == cot_num && lhs->numval() == 0
         && is_static_wb_flag_read(e->y) )
      return true;
    return false;
  }

  // The hoisted flag definition immediately preceding the guard `if`:
  // `v_flag = (writeBarrier.enabled == 0)` or `!(...)`. Returns the
  // statement so it can be removed together with the `if` when every use of
  // v_flag vanishes.
  static cinsn_t *find_hoisted_wb_flag_def(cblock_t *block, cblock_t::iterator if_it,
                                           int *out_flag_lvar)
  {
    if ( out_flag_lvar != nullptr )
      *out_flag_lvar = -1;
    if ( block == nullptr || if_it == block->begin() )
      return nullptr;
    auto prev = if_it;
    --prev;
    if ( prev->op != cit_expr || prev->cexpr == nullptr || prev->cexpr->op != cot_asg
         || prev->cexpr->x == nullptr || prev->cexpr->y == nullptr )
      return nullptr;
    const cexpr_t *lhs = strip_cast(prev->cexpr->x);
    if ( lhs == nullptr || lhs->op != cot_var )
      return nullptr;
    const cexpr_t *rhs = strip_cast(prev->cexpr->y);
    bool is_flag_def = false;
    if ( is_wb_flag_cmp_zero(rhs, cot_eq) || is_wb_flag_cmp_zero(rhs, cot_ne) )
      is_flag_def = true;
    else if ( rhs != nullptr && rhs->op == cot_lnot )
    {
      const cexpr_t *inner = strip_cast(rhs->x);
      is_flag_def = is_wb_flag_cmp_zero(inner, cot_eq) || is_wb_flag_cmp_zero(inner, cot_ne)
        || (inner != nullptr && inner->op == cot_obj && !is_go_runtime_global_ea(inner->obj_ea));
    }
    else if ( rhs != nullptr && rhs->op == cot_obj && !is_go_runtime_global_ea(rhs->obj_ea) )
      is_flag_def = true; // The raw flag read without the compare
    if ( !is_flag_def )
      return nullptr;
    if ( out_flag_lvar != nullptr )
      *out_flag_lvar = int(lhs->v.idx);
    return &*prev;
  }

  static bool block_has_any_label(const cinsn_t *branch)
  {
    if ( branch == nullptr )
      return false;
    struct label_finder_t final : public ctree_visitor_t
    {
      bool found = false;
      label_finder_t() : ctree_visitor_t(CV_FAST) {}
      int idaapi visit_insn(cinsn_t *i) override
      {
        if ( i->label_num >= 0 )
          found = true;
        return found ? 1 : 0;
      }
    };
    label_finder_t finder;
    finder.apply_to(const_cast<cinsn_t *>(branch), nullptr);
    return finder.found;
  }

  // Does the `if` subtree contain a call at `call_ea`? (Stale-blob defense:
  // the mark's call must be inside the marked guard.)
  static bool subtree_contains_ea(const cinsn_t *insn, ea_t want)
  {
    if ( insn == nullptr || want == BADADDR )
      return false;
    struct ea_finder_t final : public ctree_visitor_t
    {
      ea_t want;
      bool found = false;
      ea_finder_t(ea_t w) : ctree_visitor_t(CV_FAST), want(w) {}
      int idaapi visit_expr(cexpr_t *e) override
      {
        if ( e->ea == want )
          found = true;
        return found ? 1 : 0;
      }
    };
    ea_finder_t finder(want);
    finder.apply_to(const_cast<cinsn_t *>(insn), nullptr);
    return finder.found;
  }

  // Consume one proven WB mark: remove the guard `if` (and the hoisted flag
  // definition when its uses vanish), keep the duplicated post-if nil check
  // visible, attach `// go: write barrier` to the post-barrier store.
  void consume_wb_mark(cblock_t *block, cblock_t::iterator it, const loader::go_wb_site_ir_t &site)
  {
    cinsn_t *if_insn = &*it;
    const ea_t call_ea = cfunc->entry_ea + site.call_offset;

    go_wb_detail::wb_skip_reason_t skip = go_wb_detail::wb_skip_reason_t::none;
    if ( if_insn->cif->ielse != nullptr )
      skip = go_wb_detail::wb_skip_reason_t::has_else;
    else if ( block_has_any_label(if_insn->cif->ithen) || if_insn->label_num >= 0 )
      skip = go_wb_detail::wb_skip_reason_t::inner_label;
    else
    {
      insn_t decoded;
      if ( decode_insn(&decoded, call_ea) <= 0 || decoded.itype != wasmhex_call )
        skip = go_wb_detail::wb_skip_reason_t::stale_mark;
      else if ( !subtree_contains_ea(if_insn, call_ea) )
        skip = go_wb_detail::wb_skip_reason_t::call_not_in_if;
    }

    // Presentation placement: the post-barrier pointer store follows the
    // `if` (optionally behind the compiler-duplicated nil-check guard when
    // the mark proves an in-body guard).
    cblock_t::iterator store_it = it;
    if ( skip == go_wb_detail::wb_skip_reason_t::none )
    {
      ++store_it;
      if ( site.has_in_body_nil_guard )
      {
        // The compiler duplicates the nil-check guard right after the
        // barrier region. When the duplicate did not survive structuring
        // the in-body guard would vanish with the `if` — fail closed.
        if ( store_it == block->end() || store_it->op != cit_if || store_it->cif == nullptr )
          skip = go_wb_detail::wb_skip_reason_t::missing_post_store;
        else
          ++store_it; // keep the duplicated nil-check guard visible
      }
      const cexpr_t *lhs = nullptr;
      if ( store_it != block->end() && store_it->op == cit_expr && store_it->cexpr != nullptr
           && store_it->cexpr->op == cot_asg && store_it->cexpr->x != nullptr
           && store_it->cexpr->y != nullptr && !expr_has_side_effects(store_it->cexpr->x)
           && !expr_has_side_effects(store_it->cexpr->y) )
        lhs = strip_casts_and_lodword(store_it->cexpr->x);
      const bool non_frame_store = lhs != nullptr
        && (lhs->op == cot_ptr || lhs->op == cot_idx
            || (lhs->op == cot_obj && is_linear_memory_object(lhs->obj_ea)));
      if ( !non_frame_store )
        skip = go_wb_detail::wb_skip_reason_t::missing_post_store;
    }

    if ( skip != go_wb_detail::wb_skip_reason_t::none )
    {
      ++wb_stats.skipped_by_reason[std::size_t(skip)];
      if ( options::debug("go") )
      {
        msg("WASMHEX_GO_WB_SKIPPED func=%a insn=%a arity=%u reason=%s\n", cfunc->entry_ea,
            if_insn->ea, unsigned(site.arity), go_wb_detail::wb_skip_reason_name(skip));
      }
      return;
    }

    ++wb_stats.matched;
    schedule_wb_removal(if_insn);
    int flag_lvar = -1;
    cinsn_t *flag_def = find_hoisted_wb_flag_def(block, it, &flag_lvar);
    if ( flag_def != nullptr && flag_lvar >= 0 && func_var_refs[flag_lvar] == 2 )
      schedule_wb_removal(flag_def);

    const ea_t cmt_ea = store_it->ea != BADADDR                        ? store_it->ea
      : (store_it->cexpr != nullptr && store_it->cexpr->ea != BADADDR) ? store_it->cexpr->ea
                                                                       : if_insn->ea;
    if ( cmt_ea != BADADDR )
    {
      treeloc_t loc;
      loc.ea = cmt_ea;
      loc.itp = ITP_SEMI;
      cfunc->set_user_cmt(loc, "go: write barrier");
    }
    if ( options::debug("go") )
    {
      msg("WASMHEX_GO_WB_COLLAPSED func=%a insn=%a store=%a arity=%u\n", cfunc->entry_ea,
          if_insn->ea, cmt_ea, unsigned(site.arity));
    }
  }

public:
  explicit wasmhex_go_rewriter_t(cfunc_t *_cfunc) : ctree_visitor_t(CV_POST), cfunc(_cfunc) {}

  int idaapi visit_insn(cinsn_t *insn) override
  {
    if ( insn->op == cit_block && insn->cblock != nullptr )
    {
      const loader::go_function_ir_t *ir = current_go_ir();
      if ( ir == nullptr || ir->wb_sites.empty() )
        return 0;
      cblock_t *b = insn->cblock;
      for ( auto it = b->begin(); it != b->end(); ++it )
      {
        if ( to_remove.count(&*it) != 0 || it->op != cit_if || it->ea == BADADDR )
          continue;
        for ( const loader::go_wb_site_ir_t &site : ir->wb_sites )
        {
          if ( it->ea == cfunc->entry_ea + site.guard_if_offset )
          {
            consume_wb_mark(b, it, site);
            break;
          }
        }
      }
      return 0;
    }

    // Only a then-branch is ever removed - an if with an else belongs
    // to the function's real semantics and stays.
    if ( !prologue_mark_matches(insn) )
      return 0;
    // Verification hook: WASMHEX_GO_TRACE=1
    // logs every hidden guard so before/after counts can be compared on
    // real modules — the mark comes from the Go IR prologue record
    // (blob 'x' GOMS tail), not a ctree shape match.
    if ( options::debug("go") ) // Env reads live in loader/options.cpp
      msg("WASMHEX_GO_GUARD_HIDDEN func=%a insn=%a\n", cfunc->entry_ea, insn->ea);
    to_remove.insert(insn);
    return 0;
  }

  void apply()
  {
    // load_go_module() is the version-aware canonical accessor for
    // the Go-module flag.
    if ( load_go_module() )
    {
      var_ref_counter_t counter(func_var_refs);
      counter.apply_to(&cfunc->body, nullptr);
      apply_to(&cfunc->body, nullptr);
      remove_block_statements(cfunc, to_remove);
      if ( !wb_removed_vars.empty() )
      {
        std::map<int, std::size_t> remaining_refs;
        var_ref_counter_t rem_counter(remaining_refs);
        rem_counter.apply_to(&cfunc->body, nullptr);
        lvars_t *lvars = cfunc->get_lvars();
        if ( lvars != nullptr )
        {
          for ( int idx : wb_removed_vars )
          {
            if ( idx >= 0 && size_t(idx) < lvars->size() && remaining_refs[idx] == 0
                 && !(*lvars)[size_t(idx)].is_arg_var() )
            {
              (*lvars)[size_t(idx)].clear_used();
            }
          }
        }
      }
      if ( options::debug("go") && (wb_stats.matched > 0 || wb_stats.total_skipped() > 0) )
      {
        msg("WASMHEX_GO_WB_STATS entry=%a matched=%zu skipped=%zu\n", cfunc->entry_ea,
            wb_stats.matched, wb_stats.total_skipped());
      }
    }
  }
};

} // namespace

// Entry point used by the CMAT_FINAL pipeline in ctree.cpp.
void wasmhex_apply_go_rewriter(cfunc_t *cfunc)
{
  wasmhex_go_rewriter_t rewriter(cfunc);
  rewriter.apply();
}

} // namespace wasmhex

#endif // WASMHEX_PURE_STRING_TEST
