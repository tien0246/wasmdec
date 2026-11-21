#include "ctree_float.hpp"

#include <cmath>
#include <cstring>
#include <string>

#include <hexrays.hpp>

#include "../../../ida/processor/internal.hpp"

namespace wasmhex_float_ctree
{
enum rewrite_kind_t
{
  rw_none,
  rw_compare,
  rw_minmax,
  rw_pseudo_minmax,
  rw_sign,
  rw_reinterpret,
  rw_unary,
  rw_nearest,
  rw_trunc_sat,
};

struct rewrite_t
{
  rewrite_kind_t kind = rw_none;
  const char *value_type = nullptr;
  const char *operation = nullptr;
  int result_size = 0;
  bool is_signed = false;
};

class rewriter_t final : public ctree_visitor_t
{
  cfunc_t *cfunc;
  tinfo_t f32_type;
  tinfo_t f64_type;
  tinfo_t i32_signed;
  tinfo_t i32_unsigned;
  tinfo_t i64_signed;
  tinfo_t i64_unsigned;
  bool types_valid = false;
  // In hidden (readable) bounds mode the float bit helpers render as
  // libm/C23 calls instead of inline expansions.
  bool hidden_mode = false;

  static bool is_extract_helper(const char *name)
  {
    static const char *const names[] = {
      "LOBYTE", "HIBYTE", "LOWORD", "HIWORD", "LODWORD", "HIDWORD", "BYTE0",
      "BYTE1",  "BYTE2",  "BYTE3",  "WORD0",  "WORD1",   "WORD2",   "WORD3",
    };
    for ( const char *candidate : names )
      if ( strcmp(name, candidate) == 0 )
        return true;
    return false;
  }

  static bool has_effects(const cexpr_t &expression)
  {
    if ( !expression.has_side_effects() )
      return false;
    if ( expression.op == cot_call && expression.x != nullptr && expression.x->op == cot_helper
         && expression.x->helper != nullptr && is_extract_helper(expression.x->helper)
         && expression.a != nullptr )
    {
      for ( const carg_t &argument : *expression.a )
        if ( has_effects(argument) )
          return true;
      return false;
    }
    return true;
  }

  // True when the expression reads memory through a pointer (a wasmhex
  // linear-memory load presents as *MK_FP(selector, address)). Rewrites that
  // clone their argument expressions more than once would perform such a
  // load again at every clone site, so those rewrites opt out entirely when
  // an argument contains a memory read. cot_obj is deliberately not counted:
  // it reads a named global object, and inside one single-threaded
  // decompilation with no stores between the clones its value cannot change
  // (same reasoning as has_effects above). Child operands are only followed
  // for the operators that actually have them: cexpr_t keeps x/y/z in a
  // union with n/helper/obj_ea, so touching x on e.g. cot_num would read a
  // cnumber_t* as a cexpr_t*.
  static bool has_memory_read(const cexpr_t &expression)
  {
    if ( expression.op == cot_ptr || expression.op == cot_idx || expression.op == cot_memref
         || expression.op == cot_memptr )
      return true;
    if ( op_uses_x(expression.op) && expression.x != nullptr && has_memory_read(*expression.x) )
      return true;
    if ( op_uses_y(expression.op) && expression.y != nullptr && has_memory_read(*expression.y) )
      return true;
    if ( op_uses_z(expression.op) && expression.z != nullptr && has_memory_read(*expression.z) )
      return true;
    if ( expression.op == cot_call && expression.a != nullptr )
      for ( const carg_t &argument : *expression.a )
        if ( has_memory_read(argument) )
          return true;
    return false;
  }

  static rewrite_t decode(const char *name)
  {
    if ( name == nullptr )
      return {};
    for ( const char *value_type : { "f32", "f64" } )
    {
      const std::string prefix = std::string("wasm_") + value_type + "_";
      if ( strncmp(name, prefix.c_str(), prefix.size()) != 0 )
        continue;
      const char *operation = name + prefix.size();
      for ( const char *candidate : { "eq", "ne", "lt", "gt", "le", "ge" } )
      {
        const std::string helper = std::string(candidate) + "_bits";
        if ( strcmp(operation, helper.c_str()) == 0 )
          return { rw_compare, value_type, candidate };
      }
      for ( const char *candidate : { "min", "max" } )
      {
        const std::string helper = std::string(candidate) + "_bits";
        if ( strcmp(operation, helper.c_str()) == 0 )
          return { rw_minmax, value_type, candidate };
      }
      for ( const char *candidate : { "pmin", "pmax" } )
      {
        const std::string helper = std::string(candidate) + "_bits";
        if ( strcmp(operation, helper.c_str()) == 0 )
          return { rw_pseudo_minmax, value_type, candidate };
      }
      for ( const char *candidate : { "abs", "neg", "copysign" } )
      {
        const std::string helper = std::string(candidate) + "_bits";
        if ( strcmp(operation, helper.c_str()) == 0 )
          return { rw_sign, value_type, candidate };
      }
      for ( const char *candidate : { "ceil", "floor", "trunc", "sqrt" } )
      {
        const std::string helper = std::string(candidate) + "_bits";
        if ( strcmp(operation, helper.c_str()) == 0 )
          return { rw_unary, value_type, candidate };
      }
      if ( strcmp(operation, "nearest_bits") == 0 )
        return { rw_nearest, value_type, "nearest" };
    }
    if ( strcmp(name, "wasm_i32_reinterpret_f32_bits") == 0 )
      return { rw_reinterpret, "f32", "to_int", 4 };
    if ( strcmp(name, "wasm_i64_reinterpret_f64_bits") == 0 )
      return { rw_reinterpret, "f64", "to_int", 8 };
    if ( strcmp(name, "wasm_f32_reinterpret_i32_bits") == 0 )
      return { rw_reinterpret, "f32", "to_float", 4 };
    if ( strcmp(name, "wasm_f64_reinterpret_i64_bits") == 0 )
      return { rw_reinterpret, "f64", "to_float", 8 };
    for ( int result_bits : { 32, 64 } )
      for ( const char *value_type : { "f32", "f64" } )
        for ( bool is_signed : { true, false } )
        {
          const std::string helper = std::string("wasm_i") + std::to_string(result_bits)
            + "_trunc_sat_" + value_type + (is_signed ? "_s_bits" : "_u_bits");
          if ( strcmp(name, helper.c_str()) == 0 )
            return { rw_trunc_sat, value_type, "trunc_sat", result_bits / 8, is_signed };
        }
    return {};
  }

  const tinfo_t &float_type(const char *value_type) const
  {
    return strcmp(value_type, "f32") == 0 ? f32_type : f64_type;
  }

  const tinfo_t &signed_type(int size) const { return size == 4 ? i32_signed : i64_signed; }

  const tinfo_t &unsigned_type(int size) const { return size == 4 ? i32_unsigned : i64_unsigned; }

  int float_size(const char *value_type) const { return strcmp(value_type, "f32") == 0 ? 4 : 8; }

  cexpr_t *clone(const cexpr_t &expression) const { return new cexpr_t(expression); }

  cexpr_t *number(uint64 value, const tinfo_t &type, ea_t ea) const
  {
    cexpr_t *result = make_num(value, cfunc, ea, 0, type.get_sign(), int(type.get_size()));
    if ( result != nullptr )
      result->type = type;
    return result;
  }

  cexpr_t *signed_number(int64 value, const tinfo_t &type, ea_t ea) const
  {
    return number(uint64(value), type, ea);
  }

  cexpr_t *binary(ctype_t op, cexpr_t *left, cexpr_t *right, const tinfo_t &type, ea_t ea) const
  {
    cexpr_t *result = new cexpr_t(op, left, right);
    result->ea = ea;
    result->type = type;
    return result;
  }

  cexpr_t *ternary(cexpr_t *condition, cexpr_t *yes, cexpr_t *no, const tinfo_t &type,
                   ea_t ea) const
  {
    cexpr_t *result = new cexpr_t(cot_tern, condition, yes, no);
    result->ea = ea;
    result->type = type;
    return result;
  }

  cexpr_t *comparison(ctype_t op, cexpr_t *left, cexpr_t *right, ea_t ea) const
  {
    cexpr_t *result = new cexpr_t(op, left, right);
    result->ea = ea;
    result->calc_type(true);
    return result;
  }

  cexpr_t *logical(ctype_t op, cexpr_t *left, cexpr_t *right, ea_t ea) const
  {
    cexpr_t *result = new cexpr_t(op, left, right);
    result->ea = ea;
    result->calc_type(true);
    return result;
  }

  cexpr_t *helper_call(const tinfo_t &result_type, const char *helper,
                       const cexpr_t *const *arguments, size_t count, ea_t ea) const
  {
    carglist_t *args = new carglist_t;
    for ( size_t i = 0; i < count; ++i )
    {
      carg_t argument;
      argument.consume_cexpr(new cexpr_t(*arguments[i]));
      args->push_back(argument);
    }
    cexpr_t *result = call_helper(result_type, args, "%s", helper);
    if ( result != nullptr )
      result->ea = ea;
    return result;
  }

  cexpr_t *coerce_float(const cexpr_t &argument, const char *value_type, ea_t ea) const
  {
    const cexpr_t *arguments[] = { &argument };
    return helper_call(float_type(value_type),
                       strcmp(value_type, "f32") == 0 ? "COERCE_FLOAT" : "COERCE_DOUBLE", arguments,
                       1, ea);
  }

  cexpr_t *float_bits(const cexpr_t &expression, const char *value_type, ea_t ea) const
  {
    const int size = float_size(value_type);
    const cexpr_t *arguments[] = { &expression };
    return helper_call(unsigned_type(size), size == 4 ? "LODWORD" : "COERCE_UNSIGNED_INT64",
                       arguments, 1, ea);
  }

  cexpr_t *float_number(double value, const char *value_type, ea_t ea) const
  {
    uint64 bits;
    if ( strcmp(value_type, "f32") == 0 )
    {
      const float converted = float(value);
      uint32 bits32;
      memcpy(&bits32, &converted, sizeof(bits32));
      bits = bits32;
    }
    else
    {
      memcpy(&bits, &value, sizeof(bits));
    }
    cexpr_t *number_expression = number(bits, unsigned_type(float_size(value_type)), ea);
    cexpr_t *result = coerce_float(*number_expression, value_type, ea);
    delete number_expression;
    return result;
  }

  cexpr_t *cast(cexpr_t *value, const tinfo_t &target_type, ea_t ea) const
  {
    if ( value->type.get_size() == target_type.get_size()
         && value->type.get_sign() == target_type.get_sign() )
    {
      value->type = target_type;
      return value;
    }
    cexpr_t *result = new cexpr_t(cot_cast, value);
    result->ea = ea;
    result->type = target_type;
    result->set_user_cast();
    return result;
  }

  cexpr_t *numeric_cast(cexpr_t *value, const tinfo_t &target_type, ea_t ea) const
  {
    cexpr_t *result = new cexpr_t(cot_cast, value);
    result->ea = ea;
    result->type = target_type;
    result->set_user_cast();
    return result;
  }

  cexpr_t *float_comparison(ctype_t op, cexpr_t *left, cexpr_t *right, ea_t ea) const
  {
    cexpr_t *result = comparison(op, left, right, ea);
    result->exflags |= EXFL_FPOP;
    result->calc_type(true);
    return result;
  }

  cexpr_t *nearest(cexpr_t *expression, const char *value_type)
  {
    const ea_t ea = expression->ea;
    const int size = float_size(value_type);
    const int width = size * 8;
    const int exponent_bits = size == 4 ? 8 : 11;
    const int fraction_bits = size == 4 ? 23 : 52;
    const uint64 sign_bit = uint64(1) << (width - 1);
    const uint64 quiet_bit = uint64(1) << (fraction_bits - 1);
    const uint64 exponent_mask = ((uint64(1) << exponent_bits) - 1) << fraction_bits;
    const uint64 fraction_mask = (uint64(1) << fraction_bits) - 1;
    const tinfo_t bits_type = unsigned_type(size);
    const tinfo_t integer = signed_type(size);
    cexpr_t *raw_bits = cast(clone((*expression->a)[0]), bits_type, ea);
    cexpr_t *value = coerce_float(*raw_bits, value_type, ea);

    cexpr_t *exponent
      = binary(cot_band, clone(*raw_bits), number(exponent_mask, bits_type, ea), bits_type, ea);
    cexpr_t *exponent_is_all_ones
      = comparison(cot_eq, exponent, number(exponent_mask, bits_type, ea), ea);
    cexpr_t *fraction
      = binary(cot_band, clone(*raw_bits), number(fraction_mask, bits_type, ea), bits_type, ea);
    cexpr_t *fraction_is_nonzero = comparison(cot_ne, fraction, number(0, bits_type, ea), ea);
    cexpr_t *is_nan = logical(cot_land, exponent_is_all_ones, fraction_is_nonzero, ea);
    cexpr_t *nan_result
      = binary(cot_bor, clone(*raw_bits), number(quiet_bit, bits_type, ea), bits_type, ea);

    const double threshold = double(uint64(1) << fraction_bits);
    cexpr_t *magnitude_is_large = logical(
      cot_lor,
      float_comparison(cot_sge, clone(*value), float_number(threshold, value_type, ea), ea),
      float_comparison(cot_sle, clone(*value), float_number(-threshold, value_type, ea), ea), ea);
    cexpr_t *preserve_bits = logical(cot_lor, clone(*is_nan), magnitude_is_large, ea);

    cexpr_t *base = numeric_cast(clone(*value), integer, ea);
    cexpr_t *base_float = numeric_cast(clone(*base), float_type(value_type), ea);
    cexpr_t *negative
      = float_comparison(cot_slt, clone(*value), float_number(0.0, value_type, ea), ea);
    cexpr_t *positive_fraction
      = binary(cot_fsub, clone(*value), clone(*base_float), float_type(value_type), ea);
    positive_fraction->exflags |= EXFL_FPOP;
    cexpr_t *negative_fraction
      = binary(cot_fsub, clone(*base_float), clone(*value), float_type(value_type), ea);
    negative_fraction->exflags |= EXFL_FPOP;
    cexpr_t *fractional_part
      = ternary(clone(*negative), negative_fraction, positive_fraction, float_type(value_type), ea);
    cexpr_t *half = float_number(0.5, value_type, ea);
    cexpr_t *round_up = float_comparison(cot_sgt, clone(*fractional_part), clone(*half), ea);
    cexpr_t *tie = float_comparison(cot_eq, clone(*fractional_part), clone(*half), ea);
    cexpr_t *is_odd
      = comparison(cot_ne, binary(cot_band, clone(*base), number(1, integer, ea), integer, ea),
                   number(0, integer, ea), ea);
    cexpr_t *tie_rounds_up = logical(cot_land, tie, is_odd, ea);
    round_up = logical(cot_lor, round_up, tie_rounds_up, ea);
    cexpr_t *adjustment = ternary(clone(*negative), signed_number(-1, integer, ea),
                                  number(1, integer, ea), integer, ea);
    cexpr_t *adjacent = binary(cot_add, clone(*base), adjustment, integer, ea);
    cexpr_t *rounded = ternary(round_up, adjacent, clone(*base), integer, ea);
    cexpr_t *rounded_float = numeric_cast(clone(*rounded), float_type(value_type), ea);
    cexpr_t *rounded_bits = float_bits(*rounded_float, value_type, ea);
    cexpr_t *rounds_to_zero = comparison(cot_eq, clone(*rounded), number(0, integer, ea), ea);
    cexpr_t *signed_zero_bits
      = binary(cot_band, clone(*raw_bits), number(sign_bit, bits_type, ea), bits_type, ea);
    cexpr_t *finite_result = ternary(rounds_to_zero, signed_zero_bits, rounded_bits, bits_type, ea);
    cexpr_t *preserved_bits = ternary(clone(*is_nan), nan_result, clone(*raw_bits), bits_type, ea);
    cexpr_t *result = ternary(preserve_bits, preserved_bits, finite_result, expression->type, ea);
    result->calc_type(true);

    delete raw_bits;
    delete value;
    delete is_nan;
    delete base;
    delete base_float;
    delete negative;
    delete fractional_part;
    delete half;
    delete rounded;
    delete rounded_float;
    return result;
  }

  cexpr_t *minmax(cexpr_t *expression, const char *value_type, bool is_min)
  {
    const ea_t ea = expression->ea;
    const int size = float_size(value_type);
    const int fraction_bits = size == 4 ? 23 : 52;
    const int exponent_bits = size == 4 ? 8 : 11;
    const tinfo_t bits_type = unsigned_type(size);
    const uint64 sign_bit = uint64(1) << (size * 8 - 1);
    const uint64 exponent = ((uint64(1) << exponent_bits) - 1) << fraction_bits;
    const uint64 fraction = (uint64(1) << fraction_bits) - 1;
    const uint64 quiet_bit = uint64(1) << (fraction_bits - 1);
    cexpr_t *left = cast(clone((*expression->a)[0]), bits_type, ea);
    cexpr_t *right = cast(clone((*expression->a)[1]), bits_type, ea);

    auto masked = [&](const cexpr_t &value, uint64 mask)
    {
      return binary(cot_band, clone(value), number(mask, bits_type, ea), bits_type, ea);
    };
    auto nonzero = [&](cexpr_t *value)
    {
      return comparison(cot_ne, value, number(0, bits_type, ea), ea);
    };
    auto is_nan = [&](const cexpr_t &value)
    {
      cexpr_t *exponent_matches
        = comparison(cot_eq, masked(value, exponent), number(exponent, bits_type, ea), ea);
      cexpr_t *fraction_nonzero = nonzero(masked(value, fraction));
      return logical(cot_land, exponent_matches, fraction_nonzero, ea);
    };
    auto arithmetic_nan = [&](const cexpr_t &value)
    {
      return binary(cot_bor, clone(value), number(quiet_bit, bits_type, ea), bits_type, ea);
    };

    cexpr_t *left_nan = is_nan(*left);
    cexpr_t *right_nan = is_nan(*right);
    cexpr_t *left_zero
      = comparison(cot_eq, masked(*left, sign_bit - 1), number(0, bits_type, ea), ea);
    cexpr_t *right_zero
      = comparison(cot_eq, masked(*right, sign_bit - 1), number(0, bits_type, ea), ea);
    cexpr_t *both_zero = logical(cot_land, left_zero, right_zero, ea);
    cexpr_t *left_negative = nonzero(masked(*left, sign_bit));
    cexpr_t *left_float = coerce_float(*left, value_type, ea);
    cexpr_t *right_float = coerce_float(*right, value_type, ea);
    cexpr_t *less = float_comparison(cot_slt, clone(*left_float), clone(*right_float), ea);
    delete left_float;
    delete right_float;

    cexpr_t *zero_choice = ternary(clone(*left_negative), clone(is_min ? *left : *right),
                                   clone(is_min ? *right : *left), bits_type, ea);
    cexpr_t *numeric_choice = ternary(clone(*less), clone(is_min ? *left : *right),
                                      clone(is_min ? *right : *left), bits_type, ea);
    cexpr_t *finite = ternary(both_zero, zero_choice, numeric_choice, bits_type, ea);
    cexpr_t *right_or_finite
      = ternary(clone(*right_nan), arithmetic_nan(*right), finite, bits_type, ea);
    cexpr_t *replacement
      = ternary(left_nan, arithmetic_nan(*left), right_or_finite, expression->type, ea);
    replacement->calc_type(true);

    delete left;
    delete right;
    delete right_nan;
    delete left_negative;
    delete less;
    return replacement;
  }

  cexpr_t *sign_operation(cexpr_t *expression, const char *value_type, const char *operation)
  {
    const ea_t ea = expression->ea;
    const int size = float_size(value_type);
    const tinfo_t bits_type = unsigned_type(size);
    const uint64 sign_bit = uint64(1) << (size * 8 - 1);
    cexpr_t *magnitude = cast(clone((*expression->a)[0]), bits_type, ea);
    cexpr_t *result;
    if ( strcmp(operation, "abs") == 0 )
    {
      result = binary(cot_band, magnitude, number(~sign_bit, bits_type, ea), bits_type, ea);
    }
    else if ( strcmp(operation, "neg") == 0 )
    {
      result = binary(cot_xor, magnitude, number(sign_bit, bits_type, ea), bits_type, ea);
    }
    else
    {
      cexpr_t *sign = cast(clone((*expression->a)[1]), bits_type, ea);
      cexpr_t *absolute
        = binary(cot_band, magnitude, number(~sign_bit, bits_type, ea), bits_type, ea);
      cexpr_t *sign_only = binary(cot_band, sign, number(sign_bit, bits_type, ea), bits_type, ea);
      result = binary(cot_bor, absolute, sign_only, bits_type, ea);
    }
    return cast(result, expression->type, ea);
  }

  cexpr_t *reinterpret(cexpr_t *expression, const char *value_type, bool to_float)
  {
    const ea_t ea = expression->ea;
    const tinfo_t bits_type = unsigned_type(float_size(value_type));
    if ( to_float )
    {
      cexpr_t *bits = cast(clone((*expression->a)[0]), bits_type, ea);
      cexpr_t *result = coerce_float(*bits, value_type, ea);
      delete bits;
      return result;
    }
    cexpr_t *result = float_bits((*expression->a)[0], value_type, ea);
    return cast(result, expression->type, ea);
  }

  cexpr_t *trunc_sat(cexpr_t *expression, int result_size, const char *value_type, bool is_signed)
  {
    const ea_t ea = expression->ea;
    const tinfo_t result_type = is_signed ? signed_type(result_size) : unsigned_type(result_size);
    cexpr_t *value = coerce_float((*expression->a)[0], value_type, ea);
    cexpr_t *unordered = float_comparison(cot_ne, clone(*value), clone(*value), ea);
    const int width = result_size * 8;
    cexpr_t *finite;
    if ( is_signed )
    {
      const double limit = std::ldexp(1.0, width - 1);
      cexpr_t *lower
        = float_comparison(cot_sle, clone(*value), float_number(-limit, value_type, ea), ea);
      cexpr_t *upper
        = float_comparison(cot_sge, clone(*value), float_number(limit, value_type, ea), ea);
      cexpr_t *converted = cast(clone(*value), result_type, ea);
      finite = ternary(lower, number(uint64(1) << (width - 1), result_type, ea),
                       ternary(upper, number((uint64(1) << (width - 1)) - 1, result_type, ea),
                               converted, result_type, ea),
                       result_type, ea);
    }
    else
    {
      const double limit = std::ldexp(1.0, width);
      cexpr_t *lower
        = float_comparison(cot_sle, clone(*value), float_number(0.0, value_type, ea), ea);
      cexpr_t *upper
        = float_comparison(cot_sge, clone(*value), float_number(limit, value_type, ea), ea);
      cexpr_t *converted = cast(clone(*value), result_type, ea);
      finite = ternary(
        lower, number(0, result_type, ea),
        ternary(upper, number(width == 64 ? uint64(-1) : (uint64(1) << width) - 1, result_type, ea),
                converted, result_type, ea),
        result_type, ea);
    }
    cexpr_t *replacement
      = ternary(unordered, number(0, result_type, ea), finite, expression->type, ea);
    delete value;
    return replacement;
  }

  static ctype_t comparison_op(const char *operation)
  {
    if ( strcmp(operation, "eq") == 0 )
      return cot_eq;
    if ( strcmp(operation, "ne") == 0 )
      return cot_ne;
    if ( strcmp(operation, "lt") == 0 )
      return cot_slt;
    if ( strcmp(operation, "gt") == 0 )
      return cot_sgt;
    if ( strcmp(operation, "le") == 0 )
      return cot_sle;
    return cot_sge;
  }

public:
  explicit rewriter_t(cfunc_t *_cfunc) : ctree_visitor_t(CV_POST), cfunc(_cfunc)
  {
    types_valid = f32_type.create_simple_type(BTF_FLOAT) && f64_type.create_simple_type(BTF_DOUBLE);
    i32_signed = get_int_type_by_width_and_sign(4, type_signed);
    i32_unsigned = get_int_type_by_width_and_sign(4, type_unsigned);
    i64_signed = get_int_type_by_width_and_sign(8, type_signed);
    i64_unsigned = get_int_type_by_width_and_sign(8, type_unsigned);
    netnode wasm_node(wasmhex::wasmhex_node_name, 0, false);
    wasmhex_bounds_mode_t mode = wasmhex_bounds_mode_t::explicit_bounds;
    if ( wasm_node != BADNODE && wasmhex::get_wasm_bounds_mode(wasm_node, &mode) )
      hidden_mode = mode == wasmhex_bounds_mode_t::hidden;
  }

  // Hidden readable names: C23 fminimum/fmaximum match the wasm
  // min/max NaN and signed-zero rules exactly; roundeven is half-even like
  // wasm nearest; sqrt/ceil/floor/trunc/fabs/copysign are libm equivalents.
  // Each swap clones every argument exactly once, so memory-reading
  // arguments stay safe. neg and pmin/pmax keep their inline rewrites.
  static const char *libm_name(const char *value_type, const char *operation)
  {
    const bool f32 = strcmp(value_type, "f32") == 0;
    static const struct
    {
      const char *operation;
      const char *f32_name;
      const char *f64_name;
    } map[] = {
      { "min", "fminimumf", "fminimum" },
      { "max", "fmaximumf", "fmaximum" },
      { "nearest", "roundevenf", "roundeven" },
      { "sqrt", "sqrtf", "sqrt" },
      { "ceil", "ceilf", "ceil" },
      { "floor", "floorf", "floor" },
      { "trunc", "truncf", "trunc" },
      { "abs", "fabsf", "fabs" },
      { "copysign", "copysignf", "copysign" },
    };
    for ( const auto &entry : map )
      if ( strcmp(operation, entry.operation) == 0 )
        return f32 ? entry.f32_name : entry.f64_name;
    return nullptr;
  }

  // Trunc_sat helpers only get a shorter display name in hidden mode:
  //   wasm_i32_trunc_sat_f64_u_bits -> trunc_sat_i32_u
  static bool trunc_sat_rename(const char *name, char *out, size_t out_size)
  {
    static const char *const prefixes[] = {
      "wasm_i32_trunc_sat_f",
      "wasm_i64_trunc_sat_f",
    };
    int result_bits = 0;
    const char *rest = nullptr;
    for ( const char *prefix : prefixes )
    {
      const size_t length = strlen(prefix);
      if ( strncmp(name, prefix, length) == 0 )
      {
        result_bits = strncmp(prefix, "wasm_i32", 8) == 0 ? 32 : 64;
        rest = name + length;
        break;
      }
    }
    if ( result_bits == 0 || rest == nullptr )
      return false;
    if ( strncmp(rest, "32_", 3) != 0 && strncmp(rest, "64_", 3) != 0 )
      return false;
    rest += 3;
    if ( (rest[0] != 's' && rest[0] != 'u') || strcmp(rest + 1, "_bits") != 0 )
      return false;
    qsnprintf(out, out_size, "trunc_sat_i%d_%c", result_bits, rest[0]);
    return true;
  }

  int idaapi leave_expr(cexpr_t *expression) override
  {
    if ( !types_valid || expression->op != cot_call || expression->x == nullptr
         || expression->x->op != cot_helper || expression->x->helper == nullptr
         || expression->a == nullptr )
      return 0;
    const rewrite_t rewrite = decode(expression->x->helper);
    if ( rewrite.kind == rw_none )
      return 0;
    // Hidden readable mode: swap the bit helper for the equivalent
    // libm/C23 call (or a short trunc_sat name). Every swap clones each
    // argument exactly once, so side-effecting/memory-reading arguments
    // remain safe.
    if ( hidden_mode )
    {
      const char *renamed = nullptr;
      char trunc_sat[32];
      if ( rewrite.kind == rw_trunc_sat
           && trunc_sat_rename(expression->x->helper, trunc_sat, sizeof(trunc_sat)) )
      {
        renamed = trunc_sat;
      }
      else if ( rewrite.kind == rw_minmax || rewrite.kind == rw_unary
                || rewrite.kind == rw_nearest )
      {
        renamed = libm_name(rewrite.value_type, rewrite.operation);
      }
      else if ( rewrite.kind == rw_sign && strcmp(rewrite.operation, "neg") != 0 )
      {
        renamed = libm_name(rewrite.value_type, rewrite.operation);
      }
      if ( renamed != nullptr )
      {
        const size_t expected_arguments = rewrite.kind == rw_minmax
            || (rewrite.kind == rw_sign && strcmp(rewrite.operation, "copysign") == 0)
          ? 2
          : 1;
        if ( rewrite.kind != rw_trunc_sat && expression->a->size() != expected_arguments )
          renamed = nullptr;
      }
      if ( renamed != nullptr )
      {
        carglist_t *arguments = new carglist_t;
        for ( const carg_t &argument : *expression->a )
        {
          carg_t cloned;
          cloned.consume_cexpr(new cexpr_t(argument));
          arguments->push_back(cloned);
        }
        cexpr_t *replacement = call_helper(expression->type, arguments, "%s", renamed);
        if ( replacement != nullptr )
        {
          replacement->ea = expression->ea;
          expression->replace_by(replacement);
        }
        return 0;
      }
    }
    // Kinds whose rewrite clones each argument expression more than once
    // (minmax ~8 clones/argument, pseudo-minmax 2, nearest >10, trunc_sat
    // ~8). Those additionally require memory-read-free arguments: a clone
    // of *ptr would re-read linear memory at every use site. The 1:1 kinds
    // (compare, sign, reinterpret, unary) clone each argument at most once
    // and keep the historical has_effects-only guard.
    const bool multi_read_rewrite = rewrite.kind == rw_minmax || rewrite.kind == rw_pseudo_minmax
      || rewrite.kind == rw_nearest || rewrite.kind == rw_trunc_sat;
    for ( const carg_t &argument : *expression->a )
      if ( has_effects(argument) || (multi_read_rewrite && has_memory_read(argument)) )
        return 0;
    if ( expression->type.is_void() )
    {
      expression->replace_by(new cexpr_t());
      return 0;
    }
    const int size = float_size(rewrite.value_type);
    cexpr_t *replacement = nullptr;
    switch ( rewrite.kind )
    {
      case rw_compare:
        if ( expression->a->size() != 2 || expression->type.get_size() != 4
             || (*expression->a)[0].type.get_size() != size
             || (*expression->a)[1].type.get_size() != size )
          return 0;
        replacement = binary(comparison_op(rewrite.operation),
                             coerce_float((*expression->a)[0], rewrite.value_type, expression->ea),
                             coerce_float((*expression->a)[1], rewrite.value_type, expression->ea),
                             expression->type, expression->ea);
        replacement->exflags |= EXFL_FPOP;
        replacement->calc_type(true);
        break;

      case rw_minmax:
        if ( expression->a->size() != 2 || expression->type.get_size() != size
             || (*expression->a)[0].type.get_size() != size
             || (*expression->a)[1].type.get_size() != size )
          return 0;
        replacement = minmax(expression, rewrite.value_type, strcmp(rewrite.operation, "min") == 0);
        break;

      case rw_pseudo_minmax:
        if ( expression->a->size() != 2 || expression->type.get_size() != size
             || (*expression->a)[0].type.get_size() != size
             || (*expression->a)[1].type.get_size() != size )
          return 0;
        {
          cexpr_t *left = clone((*expression->a)[0]);
          cexpr_t *right = clone((*expression->a)[1]);
          cexpr_t *left_float = coerce_float(*left, rewrite.value_type, expression->ea);
          cexpr_t *right_float = coerce_float(*right, rewrite.value_type, expression->ea);
          const bool is_min = strcmp(rewrite.operation, "pmin") == 0;
          cexpr_t *condition = is_min
            ? float_comparison(cot_slt, right_float, left_float, expression->ea)
            : float_comparison(cot_slt, left_float, right_float, expression->ea);
          replacement
            = ternary(condition, clone(*right), clone(*left), expression->type, expression->ea);
          replacement->calc_type(true);
          delete left;
          delete right;
        }
        break;

      case rw_sign:
        if ( expression->a->size() != (strcmp(rewrite.operation, "copysign") == 0 ? 2 : 1)
             || expression->type.get_size() != size || (*expression->a)[0].type.get_size() != size
             || (expression->a->size() == 2 && (*expression->a)[1].type.get_size() != size) )
          return 0;
        replacement = sign_operation(expression, rewrite.value_type, rewrite.operation);
        break;

      case rw_reinterpret:
        if ( expression->a->size() != 1 || expression->type.get_size() != rewrite.result_size
             || (*expression->a)[0].type.get_size() != rewrite.result_size )
          return 0;
        replacement
          = reinterpret(expression, rewrite.value_type, strcmp(rewrite.operation, "to_float") == 0);
        break;

      case rw_unary:
        if ( expression->a->size() != 1 || expression->type.get_size() != size
             || (*expression->a)[0].type.get_size() != size )
          return 0;
        {
          const int fraction_bits = size == 4 ? 23 : 52;
          const int exponent_bits = size == 4 ? 8 : 11;
          const tinfo_t bits_type = unsigned_type(size);
          const uint64 exponent_mask = ((uint64(1) << exponent_bits) - 1) << fraction_bits;
          const uint64 fraction_mask = (uint64(1) << fraction_bits) - 1;
          const uint64 quiet_bit = uint64(1) << (fraction_bits - 1);
          // Host math helpers do not guarantee WebAssembly's canonical NaN cases.
          cexpr_t *raw = cast(clone((*expression->a)[0]), bits_type, expression->ea);
          cexpr_t *value = coerce_float(*raw, rewrite.value_type, expression->ea);
          const char *helper = rewrite.operation;
          if ( size == 4 )
          {
            static const char *const names[] = { "ceil", "floor", "trunc", "sqrt" };
            static const char *const names_f[] = { "ceilf", "floorf", "truncf", "sqrtf" };
            for ( size_t i = 0; i < qnumber(names); ++i )
              if ( strcmp(helper, names[i]) == 0 )
                helper = names_f[i];
          }
          const cexpr_t *arguments[] = { value };
          cexpr_t *result
            = helper_call(float_type(rewrite.value_type), helper, arguments, 1, expression->ea);
          cexpr_t *result_bits = float_bits(*result, rewrite.value_type, expression->ea);

          cexpr_t *exponent
            = binary(cot_band, clone(*raw), number(exponent_mask, bits_type, expression->ea),
                     bits_type, expression->ea);
          cexpr_t *exponent_is_all_ones = comparison(
            cot_eq, exponent, number(exponent_mask, bits_type, expression->ea), expression->ea);
          cexpr_t *fraction
            = binary(cot_band, clone(*raw), number(fraction_mask, bits_type, expression->ea),
                     bits_type, expression->ea);
          cexpr_t *fraction_is_nonzero
            = comparison(cot_ne, fraction, number(0, bits_type, expression->ea), expression->ea);
          cexpr_t *is_nan
            = logical(cot_land, exponent_is_all_ones, fraction_is_nonzero, expression->ea);
          cexpr_t *nan_result
            = binary(cot_bor, clone(*raw), number(quiet_bit, bits_type, expression->ea), bits_type,
                     expression->ea);

          if ( strcmp(rewrite.operation, "sqrt") == 0 )
          {
            const uint64 canonical_nan = exponent_mask | quiet_bit;
            cexpr_t *negative = float_comparison(
              cot_slt, clone(*value), float_number(0.0, rewrite.value_type, expression->ea),
              expression->ea);
            result_bits = ternary(negative, number(canonical_nan, bits_type, expression->ea),
                                  result_bits, bits_type, expression->ea);
          }
          replacement = ternary(is_nan, nan_result, result_bits, bits_type, expression->ea);

          delete raw;
          delete value;
          delete result;
        }
        break;

      case rw_nearest:
        if ( expression->a->size() != 1 || expression->type.get_size() != size
             || (*expression->a)[0].type.get_size() != size )
          return 0;
        replacement = nearest(expression, rewrite.value_type);
        break;

      case rw_trunc_sat:
        if ( expression->a->size() != 1 || expression->type.get_size() != rewrite.result_size
             || expression->type.get_sign() != (rewrite.is_signed ? type_signed : type_unsigned)
             || (*expression->a)[0].type.get_size() != size )
          return 0;
        replacement
          = trunc_sat(expression, rewrite.result_size, rewrite.value_type, rewrite.is_signed);
        break;

      case rw_none: return 0;
    }
    if ( replacement != nullptr )
      expression->replace_by(replacement);
    return 0;
  }
};

inline ssize_t idaapi callback(void *, hexrays_event_t event, va_list va)
{
  if ( event == hxe_maturity )
  {
    cfunc_t *cfunc = va_arg(va, cfunc_t *);
    const int maturity = va_arg(va, int);
    if ( maturity == CMAT_FINAL )
      rewriter_t(cfunc).apply_to(&cfunc->body, nullptr);
  }
  return 0;
}

inline bool &callback_installed()
{
  static bool installed = false;
  return installed;
}

bool install()
{
  if ( callback_installed() )
    return true;
  callback_installed() = install_hexrays_callback(callback, nullptr);
  return callback_installed();
}

void remove()
{
  if ( callback_installed() )
  {
    remove_hexrays_callback(callback, nullptr);
    callback_installed() = false;
  }
}
} // namespace wasmhex_float_ctree
