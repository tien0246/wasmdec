#include "ctree.hpp"

#include <algorithm>
#include <cstring>
#include <name.hpp>
#include <segment.hpp>
#include <typeinf.hpp>

#include "ctree_types.hpp"

namespace wasmhex
{
// shortcut: only pure scalar shifts/rotates/bit-counts are rewritten here; other helpers stay explicit calls.
class wasmhex_jumpout_rewriter_t final : public ctree_visitor_t
{
  struct jump_t
  {
    cinsn_t *statement;
    ea_t target;
    ea_t resolved;
  };

  cfunc_t *cfunc;
  qvector<jump_t> jumps;
  std::map<ea_t, cinsn_t *> targets;
  bool finding_targets = false;
  int next_label = 0;

  static bool resolve_target(ea_t target, ea_t *resolved)
  {
    for ( ;; )
    {
      insn_t insn;
      if ( decode_insn(&insn, target) <= 0 )
        return false;
      if ( insn.itype == wasmhex_end && is_function_end(target) )
        break;
      if ( insn.itype != wasmhex_end && insn.itype != wasmhex_else )
        break;
      target += insn.size;
    }
    *resolved = target;
    return true;
  }

public:
  explicit wasmhex_jumpout_rewriter_t(cfunc_t *_cfunc)
      : ctree_visitor_t(CV_PARENTS | CV_POST), cfunc(_cfunc)
  {
  }

  int idaapi visit_insn(cinsn_t *insn) override
  {
    if ( insn->label_num >= next_label )
      next_label = insn->label_num + 1;
    const bool is_jump_statement = std::any_of(jumps.begin(), jumps.end(), [&](const jump_t &jump)
                                               { return jump.statement == insn; });
    if ( finding_targets && !is_jump_statement )
      for ( auto &[resolved_target, statement] : targets )
        if ( insn->ea >= resolved_target && (statement == nullptr || insn->ea < statement->ea) )
          statement = insn;
    return 0;
  }

  int idaapi visit_expr(cexpr_t *expression) override
  {
    if ( expression->op != cot_call || expression->x == nullptr || expression->x->op != cot_helper
         || strcmp(expression->x->helper, "JUMPOUT") != 0 || expression->a == nullptr
         || expression->a->size() != 1 || (*expression->a)[0].op != cot_num )
      return 0;
    cinsn_t *statement = parent_insn();
    if ( statement == nullptr || statement->op != cit_expr )
      return 0;
    const ea_t target = ea_t((*expression->a)[0].numval());
    ea_t resolved = target;
    if ( !resolve_target(target, &resolved) )
      return 0;
    for ( const jump_t &jump : jumps )
      if ( jump.statement == statement )
        return 0;
    jumps.push_back({ statement, target, resolved });
    if ( targets.find(resolved) == targets.end() )
      targets[resolved] = nullptr;
    return 0;
  }

  bool apply()
  {
    if ( jumps.empty() || next_label < 0 )
      return false;
    func_entry_info_t function;
    if ( !get_func_entry_info(&function, cfunc->entry_ea) )
      return false;
    for ( const jump_t &jump : jumps )
      if ( jump.target < function.start_ea || jump.target >= function.end_ea )
        return false;
    finding_targets = true;
    apply_to(&cfunc->body, nullptr);
    finding_targets = false;
    for ( const auto &[target, statement] : targets )
      if ( statement == nullptr )
        return false;

    for ( auto &[target, statement] : targets )
      if ( statement->label_num < 0 )
        statement->label_num = next_label++;
    for ( const jump_t &jump : jumps )
    {
      const int label = targets[jump.resolved]->label_num;
      jump.statement->cleanup();
      jump.statement->op = cit_goto;
      jump.statement->cgoto = new cgoto_t();
      jump.statement->cgoto->label_num = label;
    }
    msg("wasmhex: jumpout safety net rewrote %d jump(s) at %a\n", int(jumps.size()),
        cfunc->entry_ea);
    return true;
  }
};

// Entry point used by the CMAT_FINAL pipeline in ctree.cpp.
void wasmhex_apply_jumpout_rewriter(cfunc_t *cfunc)
{
  wasmhex_jumpout_rewriter_t rewriter(cfunc);
  rewriter.apply_to(&cfunc->body, nullptr);
}

} // namespace wasmhex
