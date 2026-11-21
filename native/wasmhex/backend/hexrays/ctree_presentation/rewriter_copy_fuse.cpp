#include "ctree.hpp"

#include <name.hpp>
#include <segment.hpp>
#include <typeinf.hpp>

#include "ctree_types.hpp"

namespace wasmhex
{

// Erase the given statements from any block (defined in ctree.cpp
// shared by the rewriter translation units).
void remove_block_statements(cfunc_t *cfunc, const std::set<cinsn_t *> &targets);
// Fuse single-use temporaries. Pattern (from the golden output):
//   v62 = wasm_select_f64(a, b, c);   // or any expression
//   v53 = v62;
// The copy statement adds a variable and a line for nothing. When the
// source is single-def single-use and the two statements are adjacent in
// the same block, rewrite the first statement to assign the destination
// directly and drop the copy.
class wasmhex_copy_fuse_rewriter_t final : public ctree_visitor_t
{
  cfunc_t *cfunc;
  std::map<int, int> reads, writes;
  std::map<int, cinsn_t *> def_stmt;
  std::set<cinsn_t *> to_remove;

  struct counter_t final : public ctree_visitor_t
  {
    std::map<int, int> &reads, &writes;
    std::map<int, cinsn_t *> &def_stmt;
    counter_t(std::map<int, int> &r, std::map<int, int> &w, std::map<int, cinsn_t *> &d)
        : ctree_visitor_t(CV_POST | CV_PARENTS), reads(r), writes(w), def_stmt(d)
    {
    }

    static bool asg_family(ctype_t op) { return is_assignment(op); }

    // Ticket-2 fix: HIDWORD(v)/LODWORD(v)/__PAIR64__(a,b)/__SET_PAIR__
    // render as cot_helper calls whose ARGUMENT LIST is not traversed by
    // ctree_visitor_t - a variable only used through such a macro went
    // uncounted and the copy-fuse pass deleted its definition. Count every
    // variable inside a helper/call argument list as a read.
    void count_arg_reads(cexpr_t *e)
    {
      if ( e == nullptr )
        return;
      if ( e->op == cot_var )
        reads[int(e->v.idx)]++;
      count_arg_reads(e->x);
      count_arg_reads(e->y);
      if ( e->a != nullptr )
        for ( size_t i = 0; i < e->a->size(); ++i )
          count_arg_reads(&(*e->a)[i]);
    }

    int idaapi visit_expr(cexpr_t *e) override
    {
      if ( e->op == cot_var )
      {
        const int idx = int(e->v.idx);
        // SDK: cot_asg renders as `x = y` - x is the destination.
        bool is_write = false;
        if ( !parents.empty() )
        {
          cexpr_t *parent = (cexpr_t *)parents.back();
          if ( asg_family(parent->op) && parent->x == e )
            is_write = true;
        }
        if ( is_write )
          writes[idx]++;
        else
          reads[idx]++;
      }
      else if ( e->op == cot_helper )
      {
        if ( e->a != nullptr )
          for ( size_t i = 0; i < e->a->size(); ++i )
            count_arg_reads(&(*e->a)[i]);
      }
      return 0;
    }

    int idaapi visit_insn(cinsn_t *insn) override
    {
      if ( insn->op == cit_expr && insn->cexpr != nullptr && insn->cexpr->op == cot_asg
           && insn->cexpr->x != nullptr && insn->cexpr->x->op == cot_var )
      {
        // statement-level definition of a variable
        def_stmt[int(insn->cexpr->x->v.idx)] = insn;
      }
      return 0;
    }
  };

  static bool pure_var_copy(const cexpr_t *e, int *src_idx, int *dst_idx)
  {
    if ( e == nullptr || e->op != cot_asg )
      return false;
    const cexpr_t *dst = e->x;
    const cexpr_t *src = e->y;
    if ( dst == nullptr || src == nullptr || dst->op != cot_var || src->op != cot_var )
      return false;
    *dst_idx = int(dst->v.idx);
    *src_idx = int(src->v.idx);
    return true;
  }

public:
  explicit wasmhex_copy_fuse_rewriter_t(cfunc_t *_cfunc) : ctree_visitor_t(CV_FAST), cfunc(_cfunc)
  {
  }

  void run()
  {
    counter_t counter(reads, writes, def_stmt);
    counter.apply_to(&cfunc->body, nullptr);
    struct block_scan_t final : public ctree_visitor_t
    {
      wasmhex_copy_fuse_rewriter_t &outer;
      explicit block_scan_t(wasmhex_copy_fuse_rewriter_t &o) : ctree_visitor_t(CV_FAST), outer(o) {}
      int idaapi visit_insn(cinsn_t *insn) override
      {
        if ( insn->op != cit_block || insn->cblock == nullptr )
          return 0;
        cblock_t *b = insn->cblock;
        auto it = b->begin();
        while ( it != b->end() )
        {
          if ( outer.to_remove.count(&*it) != 0 )
          {
            ++it; // doomed copies define nothing anymore
            continue;
          }
          auto next = it;
          ++next;
          while ( next != b->end() && outer.to_remove.count(&*next) != 0 )
            ++next; // skip doomed copies so `it` can pair past them
          if ( next == b->end() )
            break;
          if ( next->label_num >= 0 )
          {
            ++it; // do not move code across a label
            continue;
          }
          // `it`: any statement-level `B = <expr>`; `next`: pure `A = B`.
          // cinsn_t::cexpr is a UNION member (12 pointer shapes: cblock/
          // cexpr/cif/.../cgoto/casm) - only cit_expr statements carry a
          // cexpr_t. A cit_goto holds a cgoto_t, a 4-byte { label_num }
          // allocation, so reading ->cexpr->op through it dereferences 4
          // bytes past the object (ASan-confirmed heap-buffer-overflow READ,
          // deterministic on any module whose ctree has a goto - gowasm-balls
          // trips it at function 4). Check the opcode before touching the
          // union, the same way every other rewriter does (e.g. the
          // cit_expr guards in rewriter_eh.cpp).
          if ( it->op != cit_expr || it->cexpr == nullptr || it->cexpr->op != cot_asg
               || it->cexpr->x == nullptr || it->cexpr->x->op != cot_var )
          {
            ++it;
            continue;
          }
          int src = -1, dst = -1;
          bool fused = false;
          // `it` must be the single definition of src; the copy in `next`
          // must be its single read. Same union guard for `next`: a goto
          // statement must never be punned into a cexpr_t (pure_var_copy
          // reads e->op first thing).
          if ( next->op == cit_expr && pure_var_copy(next->cexpr, &src, &dst) && src != dst
               && outer.writes[src] == 1 && outer.reads[src] == 1 )
          {
            auto found = outer.def_stmt.find(src);
            if ( found != outer.def_stmt.end() && found->second == &*it )
            {
              cexpr_t *def_asg = it->cexpr;
              cexpr_t *copy_asg = next->cexpr;
              // Retarget the definition's destination variable (a leaf cot_var
              // node - safe to edit in place, no allocator games) and drop the
              // copy statement.
              def_asg->x->v.idx = copy_asg->x->v.idx;
              def_asg->x->type = copy_asg->x->type;
              def_asg->type = copy_asg->type;
              outer.to_remove.insert(&*next);
              // Refresh the counters as if `next` were already erased
              // so `it` (now defining dst) can fuse with the next copy that
              // reads dst.
              outer.writes[src]--;
              outer.reads[src]--;
              outer.def_stmt.erase(src);
              outer.def_stmt[dst] = &*it;
              fused = true;
            }
          }
          if ( !fused )
            ++it; // after a fuse keep `it` in place so the chain cascades
        }
        return 0;
      }
    };
    block_scan_t scan(*this);
    scan.apply_to(&cfunc->body, nullptr);
  }

  void apply()
  {
    run();
    remove_block_statements(cfunc, to_remove);
  }
};

// Entry point used by the CMAT_FINAL pipeline in ctree.cpp.
namespace
{
// Build marker verified via strings(1) on the deployed procs dylib
// (pattern from metadata.cpp / database.cpp): this build carries the
// cit_expr union guard in the copy_fuse block scan.
extern "C" __attribute__((used)) const char kWasmhexCopyFuseUnionGuardMarker[]
  = "WASMHEX_BUILD_MARKER_FIX_COPYFUSE_UNION_GUARD";
} // namespace

void wasmhex_apply_copy_fuse_rewriter(cfunc_t *cfunc)
{
  wasmhex_copy_fuse_rewriter_t rewriter(cfunc);
  rewriter.apply();
}

} // namespace wasmhex
