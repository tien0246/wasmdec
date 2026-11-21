#include "ctree.hpp"

#include <cstring>
#include <name.hpp>
#include <segment.hpp>
#include <typeinf.hpp>

#include "ctree_types.hpp"

namespace wasmhex
{
// Emscripten's stackSave/stackRestore/stackAlloc are JS imports that
// operate on __stack_pointer on the JS side of the embedder. Rewriting them at
// the ctree level shows the real shadow-stack traffic in the pseudocode:
//   stackSave()            -> __stack_pointer          (pure read, i32)
//   stackRestore(x);       -> __stack_pointer = x;     (statement, 1:1 swap)
// stackRestore with its result used and stackAlloc(n) keep their calls: both
// need the OLD stack pointer value, which a pure expression rewrite can only
// express by injecting a fresh temporary lvar. That stays fail-closed until a
// statement-level rewrite is available.
class wasmhex_stack_import_rewriter_t final : public ctree_visitor_t
{
  cfunc_t *cfunc;
  std::set<cinsn_t *> to_remove;
  bool sp_resolved = false;
  bool sp_valid = false;
  ea_t sp_ea = BADADDR;
  tinfo_t sp_type;

  // Short import names; env_-prefixed variants appear when the same field
  // is imported from two modules. Collision-renamed imports (stackSave_N) stay
  // unrecognized on purpose (fail-closed).
  static int stack_import_kind(const char *name)
  {
    if ( name == nullptr )
      return 0;
    if ( strcmp(name, "stackSave") == 0 || strcmp(name, "env_stackSave") == 0 )
      return 1;
    if ( strcmp(name, "stackRestore") == 0 || strcmp(name, "env_stackRestore") == 0 )
      return 2;
    return 0;
  }

  // The shadow stack pointer global itself. Without a usable
  // 4-byte integral global every rewrite is skipped: the imports could be
  // operating on something else entirely.
  bool resolve_sp_global()
  {
    if ( !sp_resolved )
    {
      sp_resolved = true;
      static const char *const names[] = {
        "__stack_pointer",     "_stack_pointer",     "stack_pointer",
        "env___stack_pointer", "env__stack_pointer", "env_stack_pointer",
      };
      for ( const char *name : names )
      {
        const ea_t ea = get_name_ea(BADADDR, name);
        if ( ea == BADADDR || get_func_start(ea) != BADADDR )
          continue;
        tinfo_t type;
        if ( !get_tinfo(&type, ea) && !get_tinfo_by_flags(&type, get_flags(ea)) )
          continue;
        if ( !type.is_integral() || type.get_size() != 4 )
          continue;
        sp_ea = ea;
        sp_type = type;
        sp_valid = true;
        break;
      }
    }
    return sp_valid;
  }

  cexpr_t *sp_object(ea_t ea) const
  {
    cexpr_t *object = new cexpr_t(cot_obj, nullptr);
    object->obj_ea = sp_ea;
    object->type = sp_type;
    object->ea = ea;
    return object;
  }

  static cexpr_t *cast_to(cexpr_t *value, const tinfo_t &type, ea_t ea)
  {
    if ( value->type.get_size() == type.get_size() && value->type.get_sign() == type.get_sign() )
      return value;
    cexpr_t *result = new cexpr_t(cot_cast, value);
    result->ea = ea;
    result->type = type;
    return result;
  }

public:
  explicit wasmhex_stack_import_rewriter_t(cfunc_t *_cfunc)
      : ctree_visitor_t(CV_POST | CV_PARENTS), cfunc(_cfunc)
  {
  }

  int idaapi leave_expr(cexpr_t *expression) override
  {
    if ( expression->op != cot_call || expression->x == nullptr || expression->x->op != cot_helper
         || expression->a == nullptr || !resolve_sp_global() )
      return 0;
    const int kind = stack_import_kind(expression->x->helper);
    if ( kind == 0 )
      return 0;
    const ea_t ea = expression->ea;

    if ( kind == 1 )
    {
      // stackSave() is a pure read of __stack_pointer on the JS side.
      if ( !expression->a->empty() || expression->type.is_void()
           || expression->type.get_size() != 4 )
        return 0;
      cinsn_t *statement = parent_insn();
      if ( statement != nullptr && statement->op == cit_expr && statement->cexpr == expression )
      {
        // Discarded result: reading the global has no effect, drop the call.
        to_remove.insert(statement);
        return 0;
      }
      cexpr_t *replacement = sp_object(ea);
      if ( replacement->type.get_sign() != expression->type.get_sign() )
        replacement = cast_to(replacement, expression->type, ea);
      expression->replace_by(replacement);
      return 0;
    }

    // stackRestore(x); as a standalone statement is a 1:1 swap: the argument
    // expression is evaluated exactly once, then the store happens, exactly
    // like the imported call. Side-effecting arguments stay safe.
    if ( expression->a->size() != 1 || (*expression->a)[0].type.get_size() != 4
         || (!expression->type.is_void() && expression->type.get_size() != 4) )
      return 0;
    cinsn_t *statement = parent_insn();
    if ( statement == nullptr || statement->op != cit_expr || statement->cexpr != expression )
      return 0; // result used: the old SP value is needed, keep the call
    cexpr_t *value = cast_to(new cexpr_t((*expression->a)[0]), sp_type, ea);
    cexpr_t *assignment = new cexpr_t(cot_asg, sp_object(ea), value);
    assignment->ea = ea;
    assignment->type = sp_type;
    expression->replace_by(assignment);
    return 0;
  }

  void apply()
  {
    apply_to(&cfunc->body, nullptr);
    remove_block_statements(cfunc, to_remove);
  }
};

// Entry point used by the CMAT_FINAL pipeline in ctree.cpp.
void wasmhex_apply_stack_import_rewriter(cfunc_t *cfunc)
{
  wasmhex_stack_import_rewriter_t rewriter(cfunc);
  rewriter.apply_to(&cfunc->body, nullptr);
}

} // namespace wasmhex
