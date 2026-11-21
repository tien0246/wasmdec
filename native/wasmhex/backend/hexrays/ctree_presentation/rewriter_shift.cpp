#include "ctree.hpp"

#include <cstring>
#include <name.hpp>
#include <segment.hpp>
#include <typeinf.hpp>

#include "ctree_types.hpp"

namespace wasmhex
{
class wasmhex_shift_rewriter_t final : public ctree_visitor_t
{
  cfunc_t *cfunc;
  bool hidden_mode = false;

  cexpr_t *clone(const cexpr_t &expression) { return new cexpr_t(expression); }

  tinfo_t integer_type(int size, type_sign_t sign)
  {
    return get_int_type_by_width_and_sign(size, sign);
  }

  cexpr_t *number(uint64 value, const tinfo_t &type, ea_t ea)
  {
    cexpr_t *result = make_num(value, cfunc, ea, 0, type.get_sign(), int(type.get_size()));
    if ( result != nullptr )
      result->type = type;
    return result;
  }

  cexpr_t *binary(ctype_t op, cexpr_t *left, cexpr_t *right, const tinfo_t &type, ea_t ea)
  {
    cexpr_t *result = new cexpr_t(op, left, right);
    result->ea = ea;
    result->type = type;
    return result;
  }

  cexpr_t *cast(cexpr_t *value, const tinfo_t &type, ea_t ea)
  {
    if ( value->type.get_size() == type.get_size() && value->type.get_sign() == type.get_sign() )
    {
      value->type = type;
      return value;
    }
    cexpr_t *result = new cexpr_t(cot_cast, value);
    result->ea = ea;
    result->type = type;
    result->set_user_cast();
    return result;
  }

  cexpr_t *comparison(ctype_t op, cexpr_t *left, cexpr_t *right, ea_t ea)
  {
    cexpr_t *result = new cexpr_t(op, left, right);
    result->ea = ea;
    result->calc_type(true);
    return result;
  }

  cexpr_t *ternary(cexpr_t *condition, cexpr_t *yes, cexpr_t *no, const tinfo_t &type, ea_t ea)
  {
    cexpr_t *result = new cexpr_t(cot_tern, condition, yes, no);
    result->ea = ea;
    result->type = type;
    return result;
  }

  cexpr_t *sum_terms(qvector<cexpr_t *> &terms, const tinfo_t &type, ea_t ea)
  {
    for ( size_t step = 1; step < terms.size(); step *= 2 )
      for ( size_t i = 0; i + step < terms.size(); i += step * 2 )
        terms[i] = binary(cot_add, terms[i], terms[i + step], type, ea);
    return terms.empty() ? nullptr : terms[0];
  }

  bool decode_bitcount(const char *name, int *size, const char **operation)
  {
    if ( name == nullptr )
      return false;
    const bool i64 = strncmp(name, "wasm_i64_", 9) == 0;
    if ( !i64 && strncmp(name, "wasm_i32_", 9) != 0 )
      return false;
    const char *suffix = name + 9;
    if ( strcmp(suffix, "clz") != 0 && strcmp(suffix, "ctz") != 0 && strcmp(suffix, "popcnt") != 0 )
      return false;
    *size = i64 ? 8 : 4;
    *operation = suffix;
    return true;
  }

  cexpr_t *rewrite_bitcount(cexpr_t *expression, int size, const char *operation)
  {
    const ea_t ea = expression->ea;
    const unsigned bits = unsigned(size * 8);
    const tinfo_t type = integer_type(size, type_unsigned);
    cexpr_t *value = cast(clone((*expression->a)[0]), type, ea);
    qvector<cexpr_t *> terms;
    terms.reserve(bits);
    for ( unsigned bit = 0; bit < bits; ++bit )
    {
      if ( strcmp(operation, "clz") == 0 )
      {
        cexpr_t *condition
          = comparison(cot_ult, clone(*value), number(uint64(1) << bit, type, ea), ea);
        terms.push_back(ternary(condition, number(1, type, ea), number(0, type, ea), type, ea));
      }
      else if ( strcmp(operation, "ctz") == 0 )
      {
        const uint64 mask = bit + 1 == bits ? ~uint64(0) : (uint64(1) << (bit + 1)) - 1;
        cexpr_t *masked = binary(cot_band, clone(*value), number(mask, type, ea), type, ea);
        cexpr_t *condition = comparison(cot_eq, masked, number(0, type, ea), ea);
        terms.push_back(ternary(condition, number(1, type, ea), number(0, type, ea), type, ea));
      }
      else
      {
        cexpr_t *shifted = bit == 0
          ? clone(*value)
          : binary(cot_ushr, clone(*value), number(bit, type, ea), type, ea);
        terms.push_back(binary(cot_band, shifted, number(1, type, ea), type, ea));
      }
    }
    delete value;
    return sum_terms(terms, type, ea);
  }

  bool decode_helper(const char *name, int *size, ctype_t *op, bool *signed_right, bool *rotation,
                     bool *rotation_left)
  {
    if ( name == nullptr )
      return false;
    const bool i64 = strncmp(name, "wasm_i64_", 9) == 0;
    if ( !i64 && strncmp(name, "wasm_i32_", 9) != 0 )
      return false;
    *size = i64 ? 8 : 4;
    *signed_right = false;
    *rotation = false;
    *rotation_left = false;
    const char *operation = name + 9;
    if ( strcmp(operation, "shl") == 0 )
      *op = cot_shl;
    else if ( strcmp(operation, "shr_s") == 0 )
    {
      *op = cot_sshr;
      *signed_right = true;
    }
    else if ( strcmp(operation, "shr_u") == 0 )
      *op = cot_ushr;
    else if ( strcmp(operation, "rotl") == 0 )
    {
      *op = cot_shl;
      *rotation = true;
      *rotation_left = true;
    }
    else if ( strcmp(operation, "rotr") == 0 )
    {
      *op = cot_ushr;
      *rotation = true;
    }
    else
      return false;
    return true;
  }

  static bool decode_div_rem(const char *name, ctype_t *op, bool *unsigned_op)
  {
    if ( name == nullptr )
      return false;
    if ( strncmp(name, "wasm_i32_", 9) != 0 && strncmp(name, "wasm_i64_", 9) != 0 )
      return false;
    const char *operation = name + 9;
    *unsigned_op = false;
    if ( strcmp(operation, "div_s") == 0 )
      *op = cot_sdiv;
    else if ( strcmp(operation, "rem_s") == 0 )
      *op = cot_smod;
    else if ( strcmp(operation, "div_u") == 0 )
    {
      *op = cot_udiv;
      *unsigned_op = true;
    }
    else if ( strcmp(operation, "rem_u") == 0 )
    {
      *op = cot_umod;
      *unsigned_op = true;
    }
    else
      return false;
    return true;
  }

  static bool widenable_operand(const cexpr_t &operand)
  {
    if ( operand.op == cot_num )
      return true;
    const tinfo_t &type = operand.type;
    return type.is_bool() != 0 || (type.is_int() != 0 && type.get_size() == 1);
  }

  // std::* helper call; each argument cloned exactly once.
  cexpr_t *std_helper(const cexpr_t &call, const char *name)
  {
    if ( call.a == nullptr )
      return nullptr;
    carglist_t *arguments = new carglist_t;
    for ( const carg_t &argument : *call.a )
    {
      carg_t cloned;
      cloned.consume_cexpr(clone(argument));
      arguments->push_back(cloned);
    }
    cexpr_t *result = call_helper(call.type, arguments, "%s", name);
    if ( result != nullptr )
      result->ea = call.ea;
    return result;
  }

public:
  explicit wasmhex_shift_rewriter_t(cfunc_t *_cfunc)
      : ctree_visitor_t(CV_POST | CV_PARENTS), cfunc(_cfunc)
  {
    netnode wasm_node(wasmhex_node_name, 0, false);
    wasmhex_bounds_mode_t mode = wasmhex_bounds_mode_t::explicit_bounds;
    if ( wasm_node != BADNODE && get_wasm_bounds_mode(wasm_node, &mode) )
      hidden_mode = mode == wasmhex_bounds_mode_t::hidden;
  }

  int idaapi leave_expr(cexpr_t *expression) override
  {
    if ( expression->op != cot_call || expression->x == nullptr || expression->x->op != cot_helper
         || expression->a == nullptr )
      return 0;
    const ea_t ea = expression->ea;
    if ( hidden_mode )
    {
      ctype_t div_op;
      bool unsigned_div;
      if ( decode_div_rem(expression->x->helper, &div_op, &unsigned_div) )
      {
        if ( expression->a->size() != 2 )
          return 0;
        // A dropped division result (void expression-statement call) keeps
        // the helper: a rewritten sdiv/smod with void type fails citem
        // verification, and the bare call shows the trap honestly.
        const citem_t *parent = !parents.empty() ? parents.back() : nullptr;
        if ( expression->type.get_size() < 0 || (parent != nullptr && parent->op == cit_expr) )
          return 0;
        const int div_size = int(expression->type.get_size());
        if ( unsigned_div && (div_size != 4 && div_size != 8) )
          return 0;
        if ( unsigned_div )
        {
          // Unsigned division/modulo: cast both operands to the full
          // unsigned width so cot_udiv/cot_umod render plain / and %.
          const bool sizes_ok = (*expression->a)[0].type.get_size() == div_size
            && (*expression->a)[1].type.get_size() == div_size;
          if ( !sizes_ok || (*expression->a)[0].has_side_effects()
               || (*expression->a)[1].has_side_effects() )
            return 0;
          const tinfo_t unsigned_type = integer_type(div_size, type_unsigned);
          cexpr_t *left = cast(clone((*expression->a)[0]), unsigned_type, ea);
          cexpr_t *right = cast(clone((*expression->a)[1]), unsigned_type, ea);
          cexpr_t *replacement = binary(div_op, left, right, unsigned_type, ea);
          if ( expression->type.get_sign() != unsigned_type.get_sign() )
            replacement = cast(replacement, expression->type, ea);
          expression->replace_by(replacement);
          return 0;
        }
        cexpr_t *left = clone((*expression->a)[0]);
        cexpr_t *right = clone((*expression->a)[1]);
        if ( expression->type.get_sign() != left->type.get_sign() )
          left = cast(left, expression->type, ea);
        if ( expression->type.get_sign() != right->type.get_sign() )
          right = cast(right, expression->type, ea);
        cexpr_t *replacement = binary(div_op, left, right, expression->type, ea);
        expression->replace_by(replacement);
        return 0;
      }
    }
    int bitcount_size;
    const char *bitcount_operation;
    if ( decode_bitcount(expression->x->helper, &bitcount_size, &bitcount_operation) )
    {
      if ( expression->a->size() != 1 || expression->type.is_void()
           || expression->type.get_size() != bitcount_size )
        return 0;
      // Same value-preserving widening rule as the shift helpers
      // booleans and literals widen, plain narrower integers skip.
      {
        const cexpr_t &operand = (*expression->a)[0];
        const int operand_size = int(operand.type.get_size());
        if ( operand_size > bitcount_size
             || (operand_size != bitcount_size && !widenable_operand(operand)) )
          return 0;
      }
      if ( hidden_mode )
      {
        // clz(0)=32, ctz(0)=32 and popcount match std::countl_zero
        // std::countr_zero and std::popcount exactly; one clone each.
        const char *std_name = strcmp(bitcount_operation, "clz") == 0 ? "std::countl_zero"
          : strcmp(bitcount_operation, "ctz") == 0                    ? "std::countr_zero"
                                                                      : "std::popcount";
        cexpr_t *replacement = std_helper(*expression, std_name);
        if ( replacement != nullptr )
        {
          expression->replace_by(replacement);
          return 0;
        }
      }
      if ( (*expression->a)[0].has_side_effects() )
        return 0;
      cexpr_t *replacement = rewrite_bitcount(expression, bitcount_size, bitcount_operation);
      if ( replacement == nullptr )
        return 0;
      if ( expression->type.get_sign() != replacement->type.get_sign() )
        replacement = cast(replacement, expression->type, expression->ea);
      expression->replace_by(replacement);
      return 0;
    }
    int size;
    ctype_t op;
    bool signed_right, rotation, rotation_left;
    if ( !decode_helper(expression->x->helper, &size, &op, &signed_right, &rotation, &rotation_left)
         || expression->a->size() != 2 || expression->type.get_size() != size )
      return 0;
    // Operands narrower than the helper width are accepted when the
    // widening is value-preserving. The VALUE operand (index 0) only
    // widens for booleans and literals (a plain narrower variable's wasm
    // source could be either extend_s or extend_u — no guessing). The
    // COUNT operand (index 1) is masked mod the width by wasm semantics,
    // so its high bits never matter: any narrower integer widens
    // (zero-extension, matching the extend_i32_u the codegen emits).
    for ( size_t operand_index = 0; operand_index < expression->a->size(); ++operand_index )
    {
      const cexpr_t &operand = (*expression->a)[operand_index];
      const int operand_size = int(operand.type.get_size());
      if ( operand_size == size )
        continue;
      if ( operand_size > size )
        return 0;
      const bool is_count = operand_index == 1;
      if ( !is_count && !widenable_operand(operand) )
        return 0;
      if ( is_count && !(operand.type.is_int() != 0 || widenable_operand(operand)) )
        return 0;
    }
    if ( (*expression->a)[0].has_side_effects() || (*expression->a)[1].has_side_effects() )
      return 0;

    const tinfo_t unsigned_type = integer_type(size, type_unsigned);
    const tinfo_t operation_type = signed_right ? integer_type(size, type_signed) : unsigned_type;
    const uint64 mask_value = uint64(size * 8 - 1);
    if ( hidden_mode && rotation )
    {
      // std::rotl/std::rotr reduce the count modulo the width.
      const char *std_name = rotation_left ? "std::rotl" : "std::rotr";
      cexpr_t *replacement = std_helper(*expression, std_name);
      if ( replacement != nullptr )
      {
        expression->replace_by(replacement);
        return 0;
      }
    }
    if ( !rotation )
    {
      cexpr_t *left = cast(clone((*expression->a)[0]), operation_type, ea);
      cexpr_t *count = cast(clone((*expression->a)[1]), unsigned_type, ea);
      // A constant count already below the width makes the mask a
      // no-op - drop it.
      const bool count_masked
        = !((*expression->a)[1].op == cot_num && (*expression->a)[1].numval() <= mask_value);
      cexpr_t *shift_count = count;
      if ( count_masked )
        shift_count
          = binary(cot_band, count, number(mask_value, unsigned_type, ea), unsigned_type, ea);
      cexpr_t *replacement = binary(op, left, shift_count, operation_type, ea);
      if ( expression->type.get_sign() != replacement->type.get_sign() )
        replacement = cast(replacement, expression->type, ea);
      expression->replace_by(replacement);
      return 0;
    }

    cexpr_t *left = cast(clone((*expression->a)[0]), unsigned_type, ea);
    cexpr_t *count = cast(clone((*expression->a)[1]), unsigned_type, ea);
    cexpr_t *masked_count
      = binary(cot_band, count, number(mask_value, unsigned_type, ea), unsigned_type, ea);
    cexpr_t *opposite = binary(cot_band,
                               binary(cot_sub, number(uint64(size * 8), unsigned_type, ea),
                                      clone(*masked_count), unsigned_type, ea),
                               number(mask_value, unsigned_type, ea), unsigned_type, ea);
    cexpr_t *first = binary(rotation_left ? cot_shl : cot_ushr, clone(*left), clone(*masked_count),
                            unsigned_type, ea);
    cexpr_t *second = binary(rotation_left ? cot_ushr : cot_shl, left, opposite, unsigned_type, ea);
    delete masked_count;
    cexpr_t *replacement = binary(cot_bor, first, second, unsigned_type, ea);
    if ( expression->type.get_sign() != replacement->type.get_sign() )
      replacement = cast(replacement, expression->type, ea);
    expression->replace_by(replacement);
    return 0;
  }
};

// Entry point used by the CMAT_FINAL pipeline in ctree.cpp.
void wasmhex_apply_shift_rewriter(cfunc_t *cfunc)
{
  wasmhex_shift_rewriter_t rewriter(cfunc);
  rewriter.apply_to(&cfunc->body, nullptr);
}

} // namespace wasmhex
