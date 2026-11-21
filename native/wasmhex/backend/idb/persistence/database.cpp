#include "../../../ida/loader/internal.hpp"

#include <cstring> // memset/strlen: declare directly, never via pro.h's transitives
namespace wasmhex::loader
{
// append_uleb, split_demangled_parameters, mangled_param_type moved to core/module.cpp.

std::vector<std::string> split_demangled_parameters(const std::string &text)
{
  std::vector<bool> after_operator(text.size(), false);
  for ( size_t i = 10; i < text.size(); ++i )
    if ( text.compare(i - 10, 2, "::") == 0 && text.compare(i - 8, 8, "operator") == 0 )
      after_operator[i] = true;
  auto continues_operator = [](char previous, char current)
  {
    return (previous == '<' && current == '<') || (previous == '>' && current == '>')
      || (previous == '(' && current == ')');
  };
  size_t depth = 0;
  bool in_parens = false;
  bool any_char = false;
  std::string current;
  std::vector<std::string> parameters;
  bool prev_operator_symbol = false;
  char prev_symbol = 0;
  for ( size_t i = 0; i < text.size(); ++i )
  {
    const char c = text[i];
    if ( c == '(' || c == ')' || c == '<' || c == '>' )
    {
      const bool operator_symbol
        = after_operator[i] || (prev_operator_symbol && continues_operator(prev_symbol, c));
      prev_operator_symbol = operator_symbol;
      prev_symbol = c;
      if ( operator_symbol )
        continue;
      if ( c == '(' )
      {
        if ( depth == 0 )
        {
          in_parens = true;
          parameters.clear();
          current.clear();
          any_char = false;
          ++depth;
          prev_operator_symbol = false;
          continue; // the parameter-list opener is not part of a type
        }
        ++depth;
      }
      else if ( c == '<' )
        ++depth;
      else if ( depth > 0 )
        --depth;
      // Unlike the counter this splitter KEEPS the parameter text
      // — nested template brackets are part of the type spelling and the
      // trailing '*' survives, so "allocator<X<Y>>*" arrives intact. The
      // parameter-list parentheses themselves are not part of any type.
      if ( in_parens && depth >= 1 )
      {
        // a NESTED '(' or ')' belongs to the type; the parameter list's
        // own parentheses land at depth 0 and are never pushed
        current.push_back(c);
        any_char = true;
      }
      continue;
    }
    prev_operator_symbol = false;
    prev_symbol = 0;
    if ( !in_parens || depth < 1 )
      continue;
    if ( c == ',' && depth == 1 )
    {
      if ( any_char )
        parameters.push_back(current);
      current.clear();
      any_char = false;
      continue;
    }
    if ( c != ' ' )
    {
      any_char = true;
      current.push_back(c);
    }
  }
  if ( in_parens && any_char )
    parameters.push_back(current);
  // "(void)" is an empty parameter list.
  if ( parameters.size() == 1 && parameters[0] == "void" )
    parameters.clear();
  return parameters;
}

static std::string sanitize_opaque_struct_type(std::string text)
{
  if ( text.size() > 5 && text.compare(text.size() - 5, 5, "const") == 0 )
    text.resize(text.size() - 5);
  else if ( text.rfind("const", 0) == 0 && text.size() > 5 )
    text = text.substr(5);
  for ( char &c : text )
    if ( !(c == '_' || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')) )
      c = '_';
  while ( text.size() > 1 && text.front() == '_' )
    text.erase(0, 1);
  while ( text.size() > 1 && text.back() == '_' )
    text.pop_back();
  if ( text.empty()
       || !(text[0] == '_' || (text[0] >= 'a' && text[0] <= 'z')
            || (text[0] >= 'A' && text[0] <= 'Z')) )
    return std::string();
  return "struct " + text;
}

// Map one demangled parameter spelling to a C type usable in a
// usercall declaration, CHECKED against the wasm parameter type. wasm32
// sizes: long == 4 bytes, long long == 8. By-value optional_ptr<T,...>
// maps to `struct T *` (or pointer-mapped T); enums and unknown 4-byte
// by-value parameters return empty so the caller falls back to `c_type(wasm_type)`
// per-parameter without failing the whole prototype.
std::string mangled_param_type(const std::string &raw, uchar wasm_type)
{
  std::string text = raw;
  // pointers/references: count trailing '*' (spaces already stripped) and
  // collapse trailing '&' (lvalue or rvalue reference) to one pointer.
  size_t references = 0;
  while ( !text.empty() && text.back() == '&' )
  {
    ++references;
    text.pop_back();
  }
  size_t pointers = references > 0 ? 1 : 0;
  while ( !text.empty() && text.back() == '*' )
  {
    ++pointers;
    text.pop_back();
  }
  if ( text.find('*') != std::string::npos || text.find('&') != std::string::npos )
  {
    // If there are internal '*' or '&' outside template brackets, e.g.
    // "char*const*" or function pointers, keep only if inside '<...>'.
    size_t angle = 0;
    for ( char c : text )
    {
      if ( c == '<' )
        ++angle;
      else if ( c == '>' && angle > 0 )
        --angle;
      else if ( angle == 0 && (c == '*' || c == '&') )
        return std::string();
    }
  }
  // By-value optional_ptr<T, ...> in wasm32 is a 4-byte raw pointer
  // wrapper passed in an i32 register — unwrap to `T *`.
  if ( pointers == 0 && wasm_type == 0x7F )
  {
    std::string unqual = text;
    if ( unqual.size() > 5 && unqual.compare(unqual.size() - 5, 5, "const") == 0 )
      unqual.resize(unqual.size() - 5);
    else if ( unqual.rfind("const", 0) == 0 && unqual.size() > 5 )
      unqual = unqual.substr(5);
    const size_t opt_pos = unqual.find("optional_ptr<");
    const bool is_opt_ptr = opt_pos != std::string::npos
      && (opt_pos == 0 || (opt_pos >= 2 && unqual.compare(opt_pos - 2, 2, "::") == 0))
      && unqual.back() == '>';
    if ( is_opt_ptr )
    {
      const size_t inner_start = opt_pos + 13; // strlen("optional_ptr<")
      const size_t inner_end = unqual.size() - 1;
      size_t depth = 0;
      size_t first_comma = std::string::npos;
      for ( size_t i = inner_start; i < inner_end; ++i )
      {
        if ( unqual[i] == '<' || unqual[i] == '(' )
          ++depth;
        else if ( (unqual[i] == '>' || unqual[i] == ')') && depth > 0 )
          --depth;
        else if ( unqual[i] == ',' && depth == 0 )
        {
          first_comma = i;
          break;
        }
      }
      const std::string inner = unqual.substr(
        inner_start, (first_comma != std::string::npos ? first_comma : inner_end) - inner_start);
      if ( !inner.empty() )
        return mangled_param_type(inner + "*", wasm_type);
    }
  }
  bool is_const = false;
  {
    // The splitter already removed spaces, so both "const char" and
    // "char const" arrive glued. Only strip when the remainder is a known
    // builtin — an opaque name that merely contains "const" keeps it.
    std::string without_const;
    if ( text.rfind("const", 0) == 0 && text.size() > 5 )
      without_const = text.substr(5);
    else if ( text.size() > 5 && text.compare(text.size() - 5, 5, "const") == 0 )
      without_const = text.substr(0, text.size() - 5);
    if ( !without_const.empty() )
    {
      static const char *builtins[]
        = { "char",        "signedchar", "unsignedchar", "short",    "unsignedshort",    "int",
            "unsignedint", "long",       "unsignedlong", "longlong", "unsignedlonglong", "float",
            "double",      "bool",       "void" };
      for ( const char *builtin : builtins )
        if ( without_const == builtin )
        {
          is_const = true;
          text = without_const;
          break;
        }
    }
  }
  std::string base;
  if ( text == "char" || text == "signedchar" || text == "unsignedchar" || text == "short"
       || text == "unsignedshort" || text == "int" || text == "unsignedint" || text == "long"
       || text == "unsignedlong" || text == "longlong" || text == "unsignedlonglong"
       || text == "float" || text == "double" || text == "bool" || text == "void" )
    base = text;
  else
  {
    // An unknown base (yyjson_alc, duckdb::LogicalType, ...) is only
    // usable behind a pointer: an opaque `struct NAME *`. Sanitize the
    // name for IDA ("::" and anything non-alnum_ -> '_'), drop a trailing
    // "const" (postfix qualification), and trim decoration underscores.
    // Collapse template arguments (<...>) so the opaque struct name stays
    // concise and matches (`struct std____2__vector *`).
    if ( pointers == 0 )
      return std::string();
    if ( text.size() > 5 && text.compare(text.size() - 5, 5, "const") == 0 )
      text.resize(text.size() - 5);
    else if ( text.rfind("const", 0) == 0 && text.size() > 5 )
      text = text.substr(5);
    const size_t template_open = text.find('<');
    if ( template_open != std::string::npos )
      text.resize(template_open);
    base = sanitize_opaque_struct_type(text);
    if ( base.empty() )
      return std::string();
  }
  std::string mapped;
  if ( base == "char" )
    mapped = is_const ? "const char" : "char";
  else if ( base == "signedchar" )
    mapped = "signed char";
  else if ( base == "unsignedchar" )
    mapped = is_const ? "const unsigned char" : "unsigned char";
  else if ( base == "short" )
    mapped = is_const ? "const short" : "short";
  else if ( base == "unsignedshort" )
    mapped = is_const ? "const unsigned short" : "unsigned short";
  else if ( base == "int" || base == "long" )
    mapped = is_const ? "const int" : "int"; // wasm32: long is 4 bytes
  else if ( base == "unsignedint" || base == "unsignedlong" )
    mapped = is_const ? "const unsigned int" : "unsigned int";
  else if ( base == "longlong" )
    mapped = is_const ? "const __int64" : "__int64";
  else if ( base == "unsignedlonglong" )
    mapped = is_const ? "const unsigned __int64" : "unsigned __int64";
  else if ( base == "float" )
    mapped = is_const ? "const float" : "float";
  else if ( base == "double" )
    mapped = is_const ? "const double" : "double";
  else if ( base == "bool" )
    // wasm32 ABI carries a bool as a full i32 register; the hexrays `bool`
    // type is 1 byte wide and a register-location parameter rejects it at
    // microcode init — render the register-width int instead.
    mapped = is_const ? "const int" : "int";
  else if ( base == "void" )
    mapped = pointers > 0 ? (is_const ? "const void" : "void") : std::string();
  else
    mapped = base; // struct NAME (opaque, pointer-only)
  if ( mapped.empty() )
    return std::string();
  // Size check against the wasm parameter: every pointer is i32; the
  // by-value size must match the wasm scalar exactly.
  const bool wasm_i32 = wasm_type == 0x7F;
  const bool wasm_i64 = wasm_type == 0x7E;
  const bool wasm_f32 = wasm_type == 0x7D;
  const bool wasm_f64 = wasm_type == 0x7C;
  if ( pointers > 0 )
  {
    if ( !wasm_i32 )
      return std::string();
  }
  else if ( base == "char" || base == "signedchar" || base == "unsignedchar" || base == "short"
            || base == "unsignedshort" )
  {
    return std::string(); // no matching wasm scalar width — skip, no guessing
  }
  else if ( base == "int" || base == "long" || base == "unsignedint" || base == "unsignedlong"
            || base == "bool" )
  {
    if ( !wasm_i32 )
      return std::string();
  }
  else if ( base == "longlong" || base == "unsignedlonglong" )
  {
    if ( !wasm_i64 )
      return std::string();
  }
  else if ( base == "float" )
  {
    if ( !wasm_f32 )
      return std::string();
  }
  else if ( base == "double" )
  {
    if ( !wasm_f64 )
      return std::string();
  }
  else if ( base != "void" )
  {
    return std::string(); // opaque by value — unreachable (pointers>0 guard)
  }
  std::string result = mapped;
  for ( size_t i = 0; i < pointers; ++i )
    result += " *";
  return result;
}

static std::string extract_method_this_type(const std::string &prefix)
{
  const size_t sep = prefix.rfind("::");
  if ( sep == std::string::npos )
    return std::string();
  std::string class_name = prefix.substr(0, sep);
  const size_t space = class_name.rfind(' ');
  if ( space != std::string::npos )
    class_name = class_name.substr(space + 1);
  while ( !class_name.empty() && (class_name.front() == '*' || class_name.front() == '&') )
    class_name.erase(0, 1);
  const size_t template_open = class_name.find('<');
  if ( template_open != std::string::npos )
    class_name.resize(template_open);
  for ( char &c : class_name )
    if ( !(c == '_' || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')) )
      c = '_';
  if ( class_name.empty() || class_name == "operator" )
    return std::string();
  return "struct " + class_name + " *";
}

// Build a usercall declaration whose parameter TYPES come from
// the demangled Itanium name (wasm types supply sizes/registers). Per-param
// mapping: '&'/'*' params keep `struct X *`; by-value 4-byte params map
// best-effort (optional_ptr<T> -> `struct T *`; enum/unknown by-value ->
// c_type(wasm_type), i.e. int); never fail the whole prototype on one param.
// Also handles hidden sret param: method with wasm_params == demangled_params + 2
// (or free function + 1) -> first param is `void *__ret`.
bool make_mangled_prototype(const std::string &demangled, const std::string &prefix,
                            const wasm_type_t &type, const std::vector<qstring> *param_names,
                            qstring *decl)
{
  if ( type.results.size() > 1 || type.params.size() > wasmhex_user_regs
       || demangled.find('(') == std::string::npos )
    return false;
  const std::vector<std::string> parameters = split_demangled_parameters(demangled);
  const bool has_scope = prefix.find("::") != std::string::npos;
  bool has_sret = false;
  bool is_method = false;
  std::vector<std::string> param_types;
  if ( parameters.size() == type.params.size() )
  {
    if ( parameters.empty() )
      return false;
    param_types = parameters;
  }
  else if ( parameters.size() + 1 == type.params.size() )
  {
    if ( type.params[0] != 0x7F )
      return false;
    if ( has_scope )
    {
      // Method: the implicit `this` leads the wasm list. Its type is an
      // opaque pointer named after the class (prefix up to the final "::",
      // template arguments dropped).
      const std::string this_type = extract_method_this_type(prefix);
      if ( this_type.empty() )
        return false;
      is_method = true;
      param_types.push_back(this_type);
      param_types.insert(param_types.end(), parameters.begin(), parameters.end());
    }
    else
    {
      // Free function with hidden sret pointer (wasm_params == demangled_params + 1).
      if ( !type.results.empty() )
        return false;
      has_sret = true;
      param_types.push_back("void *");
      param_types.insert(param_types.end(), parameters.begin(), parameters.end());
    }
  }
  else if ( parameters.size() + 2 == type.params.size() && has_scope && type.results.empty() )
  {
    // Method with hidden sret pointer (wasm_params == demangled_params + 2):
    // param 0 is `void *__ret`, param 1 is `struct Class *this`.
    if ( type.params[0] != 0x7F || type.params[1] != 0x7F )
      return false;
    const std::string this_type = extract_method_this_type(prefix);
    if ( this_type.empty() )
      return false;
    has_sret = true;
    is_method = true;
    param_types.push_back("void *");
    param_types.push_back(this_type);
    param_types.insert(param_types.end(), parameters.begin(), parameters.end());
  }
  else
  {
    return false;
  }
  const char *result = type.results.empty() ? "void" : c_type(type.results[0]);
  if ( result == nullptr )
    return false;
  decl->append(result);
  decl->append(" __usercall dummy");
  const size_t result_size = type.results.empty()        ? 0
    : type.results[0] == 0x7E || type.results[0] == 0x7C ? 8
                                                         : 4;
  if ( result_size == 4 )
    decl->append("@<r0>");
  else if ( result_size == 8 )
    decl->append("@<r0:r1>");
  decl->append("(");
  size_t arg_reg = 0;
  for ( size_t i = 0; i < param_types.size(); ++i )
  {
    std::string mapped;
    if ( has_sret && i == 0 )
    {
      mapped = "void *";
    }
    else if ( is_method && i == (has_sret ? 1 : 0) )
    {
      mapped = param_types[i]; // the synthesized this pointer
    }
    else
    {
      mapped = mangled_param_type(param_types[i], type.params[i]);
      if ( mapped.empty() )
      {
        // Per-parameter fallback: unknown/enum by-value parameter keeps its
        // exact wasm scalar type (e.g. int for i32) instead of aborting the
        // whole prototype.
        const char *fallback = c_type(type.params[i]);
        if ( fallback == nullptr )
          return false;
        mapped = fallback;
      }
    }
    const size_t arg_size = type.params[i] == 0x7E || type.params[i] == 0x7C ? 8 : 4;
    const size_t regs = arg_size / 4;
    qstring argument_name;
    if ( param_names != nullptr && i < param_names->size() && !(*param_names)[i].empty() )
      argument_name = (*param_names)[i];
    else
      argument_name.sprnt("a%zu", i + 1);
    if ( has_sret && i == 0 && (argument_name == "a1" || argument_name == "this") )
      argument_name = "__ret";
    else if ( has_sret && is_method && i == 1 && argument_name == "a2" )
      argument_name = "this";
    else if ( !has_sret && is_method && i == 0 && argument_name == "a1" )
      argument_name = "this";
    if ( !append_wasm_user_argument(decl, mapped.c_str(), argument_name.c_str(), arg_reg,
                                    int(arg_size), i != 0) )
      return false;
    arg_reg += regs;
  }
  decl->append(");");
  return true;
}

const char *find_standard_import_prototype(const qstring &name, const wasm_type_t &type)
{
  for ( const auto &known : standard_import_prototypes )
  {
    if ( name == known.name )
      return known.decl;
  }
  return nullptr;
}

bool go_proven_void_results(const module_t &module, std::size_t body_index)
{
  __attribute__((used)) static const char kKeepG5S2aMarker[]
    = "WASMHEX_BUILD_MARKER_G5_VOID_RESULTS";
  if ( kKeepG5S2aMarker[0] == '\0' )
    return false;
  if ( !module.go_provenance || body_index >= module.bodies.size() )
    return false;
  const body_t &body = module.bodies[body_index];
  const go_function_ir_t &ir = body.go_ir;
  return ir.frame_valid(body.size) && ir.results.empty() && !ir.ret.empty();
}

bool go_proven_results(const module_t &module, std::size_t body_index, std::vector<uchar> *types)
{
  __attribute__((used)) static const char kKeepG5S2bMarker[]
    = "WASMHEX_BUILD_MARKER_G5_RESULT_TYPES";
  if ( kKeepG5S2bMarker[0] == '\0' )
    return false;
  if ( !module.go_provenance || body_index >= module.bodies.size() )
    return false;
  const body_t &body = module.bodies[body_index];
  const go_function_ir_t &ir = body.go_ir;
  if ( !ir.frame_valid(body.size) || !ir.results_caller_proven() )
    return false;
  return go_result_slot_types(ir, types) && !types->empty();
}

bool go_named_sret_pointee(const module_t &module, std::size_t body_index,
                           const std::string &function_name, std::string *c_name,
                           std::vector<go_c_field_t> *c_fields, std::uint64_t *c_size)
{
  __attribute__((used)) static const char kKeepG7S3Marker[] = "WASMHEX_BUILD_MARKER_G7_NAMED_SRET";
  if ( kKeepG7S3Marker[0] == '\0' )
    return false;
  if ( c_name == nullptr || !module.go_provenance || body_index >= module.bodies.size() )
    return false;
  // The sret `result` parameter is only nameable when the caller
  // evidence proves the slot. When a caller ArgInfo group binds the
  // phantom slot at the wrong window offset (fmt.Sprintf: the variadic
  // slice at SP+16 landed on `result` while callers read the string at
  // SP+40), the named pointee would certify a lie — downgrade (the sret
  // pointer stays `void *` and the parameter loses the `result` name).
  if ( go_sret_result_slot_verdict(module, body_index) == go_sret_slot_verdict_t::kBoundArgument )
    return false;
  std::vector<uchar> proven;
  if ( !go_proven_results(module, body_index, &proven) || proven.size() <= 1 )
    return false; // only the caller-proven multi-result display is refined
  module_t &mutable_module = const_cast<module_t &>(module);
  go_named_sig_t sig;
  if ( !go_resolve_named_signature(mutable_module, body_index, function_name, &sig) || !sig.complete
       || sig.results.size() != 1 )
    return false; // a Go tuple result is not one named composite: keep void *
  const go_sig_param_t &result = sig.results[0];
  if ( result.c_name.empty() )
    return false;
  std::uint64_t slot_bytes = 0;
  for ( uchar type : proven )
    slot_bytes += type == 0x7E || type == 0x7C ? 8 : 4;
  if ( slot_bytes != result.size )
    return false; // the anchor and the frame evidence disagree
  go_c_struct_t layout;
  if ( !go_c_struct_for_param(mutable_module, result, &layout) || layout.fields.empty() )
    return false;
  *c_name = layout.c_name;
  if ( c_fields != nullptr )
    *c_fields = layout.fields;
  if ( c_size != nullptr )
    *c_size = layout.size;
  return true;
}

bool make_prototype(const wasm_type_t &type, const qstring &name, qstring *decl,
                    const std::vector<qstring> *param_names)
{
  if ( type.results.size() > 1 )
    return false;
  const char *result = type.results.empty() ? "void" : c_type(type.results[0]);
  if ( result == nullptr || type.params.size() > wasmhex_user_regs )
    return false;
  decl->append(result);
  decl->append(" __usercall dummy");
  const size_t result_size = type.results.empty()        ? 0
    : type.results[0] == 0x7E || type.results[0] == 0x7C ? 8
                                                         : 4;
  if ( result_size == 4 )
    decl->append("@<r0>");
  else if ( result_size == 8 )
    decl->append("@<r0:r1>");
  decl->append("(");
  size_t arg_reg = 0;
  for ( size_t i = 0; i < type.params.size(); ++i )
  {
    const char *param = c_type(type.params[i]);
    if ( param == nullptr )
      return false;
    const size_t arg_size = type.params[i] == 0x7E || type.params[i] == 0x7C ? 8 : 4;
    const size_t regs = arg_size / 4;
    qstring argument_name;
    if ( param_names != nullptr && i < param_names->size() && !(*param_names)[i].empty() )
      argument_name = (*param_names)[i];
    else
      argument_name.sprnt("a%zu", i + 1);
    if ( !append_wasm_user_argument(decl, param, argument_name.c_str(), arg_reg, int(arg_size),
                                    i != 0) )
      return false;
    arg_reg += regs;
  }
  if ( type.params.empty() )
    decl->append("void");
  decl->append(");");
  return true;
}

// tuple_type_name / make_multi_return_location / tuple_layout_matches /
// get_multi_result_type moved to ida/abi.cpp.

bool make_multi_prototype(const wasm_type_t &type, tinfo_t *out, bool force_sret,
                          const char *sret_pointee, const char *result_name)
{
  size_t reference_count = 0;
  for ( size_t i = 0; i < type.results.size(); ++i )
    if ( type.results[i] != 0x7F && type.results[i] != 0x7E && type.results[i] != 0x7D
         && type.results[i] != 0x7C
         && (!is_abstract_reference_handle(type.results[i], type.result_heap_types[i])
             || ++reference_count > 1) )
      return false;
  tinfo_t tuple_type;
  func_type_data_t function;
  size_t result_size = 0;
  for ( uchar result : type.results )
    result_size += result == 0x7E || result == 0x7C ? 8 : 4;
  const bool sret = force_sret || result_size > 16;
  size_t words = 0;
  if ( !multi_result_word_count(type.results, &words, sret) )
    return false;
  (void)words;
  if ( sret )
  {
    qstring result_decl("void *");
    if ( sret_pointee != nullptr && sret_pointee[0] != '\0' )
      result_decl.sprnt("struct %s *", sret_pointee);
    qstring downgraded_name;
    const char *sret_argument_name = result_name;
    if ( sret_argument_name == nullptr )
    {
      downgraded_name.sprnt("a%zu", type.params.size() + 1);
      sret_argument_name = downgraded_name.c_str();
    }
    qstring declaration("void __usercall wasmhex_sret_type(");
    size_t arg_register = 0;
    for ( size_t i = 0; i < type.params.size(); ++i )
    {
      const char *decl = c_type(type.params[i]);
      const size_t size = type.params[i] == 0x7E || type.params[i] == 0x7C ? 8 : 4;
      qstring name;
      name.sprnt("a%zu", i + 1);
      if ( decl == nullptr
           || !append_wasm_user_argument(&declaration, decl, name.c_str(), arg_register, int(size),
                                         i != 0) )
        return false;
      arg_register += wasm_argument_word_count(arg_register, int(size));
    }
    if ( !append_wasm_user_argument(&declaration, result_decl.c_str(), sret_argument_name,
                                    arg_register, 4, !type.params.empty()) )
      return false;
    declaration.append(");");
    tinfo_t sret_type;
    func_type_data_t sret_info;
    const bool parsed
      = parse_decl(&sret_type, nullptr, get_idati(), declaration.c_str(), PT_SIL | PT_SEMICOLON);
    const bool got_details = parsed && sret_type.get_func_details(&sret_info, GTD_CALC_ARGLOCS);
    // The parsed sret pointer type follows the DB pointer size (4 on 32-bit
    // databases, 8 on wide ones); the declared usercall location still pins
    // the 4-byte wasm32 argument slot, so accept either pointer size.
    const int sret_ptr_size = got_details ? int(sret_info[type.params.size()].type.get_size()) : 0;
    if ( got_details && sret_info.size() == type.params.size() + 1 && sret_info.rettype.is_void()
         && sret_info[type.params.size()].type.is_ptr()
         && (sret_ptr_size == 4 || sret_ptr_size == 8) )
    {
      *out = sret_type;
      return true;
    }
    // A forced-sret Go display never falls back to the tuple form —
    // tuple-returning callees break the raw i32 call sites.
    if ( force_sret )
      return false;
  }
  if ( !get_multi_result_type(type.results, &tuple_type) )
    return false;
  function.rettype = tuple_type;
  if ( !make_multi_return_location(type.results, &function.retloc) )
    return false;
  function.set_cc(CM_CC_SPECIAL);
  function.flags |= FTI_ARGLOCS | FTI_EXPLOCS;

  size_t arg_register = 0;
  for ( size_t i = 0; i < type.params.size(); ++i )
  {
    const char *decl = c_type(type.params[i]);
    if ( decl == nullptr )
      return false;
    tinfo_t param_type;
    if ( !parse_decl(&param_type, nullptr, get_idati(), decl, PT_SIL | PT_SEMICOLON) )
      return false;
    const size_t size = param_type.get_size();
    argloc_t argloc;
    if ( !make_wasm_argument_location(arg_register, int(size), &argloc) )
      return false;
    qstring arg_name;
    arg_name.sprnt("a%zu", i + 1);
    function.push_back(funcarg_t(arg_name.c_str(), param_type, argloc));
    arg_register += wasm_argument_word_count(arg_register, int(size));
  }
  return out->create_func(function);
}

bool store_type_vector(netnode &node, uint32 index, uchar tag, const std::vector<uchar> &types)
{
  bytevec encoded;
  append_uleb(&encoded, types.size());
  for ( uchar type : types )
    encoded.push_back(type);
  return node.supset(index, encoded.data(), encoded.size(), tag);
}

bool store_type_vector(netnode &node, ea_t ea, uchar tag, const std::vector<uchar> &types)
{
  bytevec encoded;
  append_uleb(&encoded, types.size());
  for ( uchar type : types )
    encoded.push_back(type);
  return node.supset_ea(ea, encoded.data(), encoded.size(), tag);
}

bool store_reference_metadata(netnode &node, uint32 index, uchar tag,
                              const std::vector<int64> &heap_types,
                              const std::vector<uchar> &ref_forms)
{
  bytevec encoded;
  if ( !encode_reference_metadata(&encoded, heap_types, ref_forms) )
    return false;
  return encoded.empty() || node.supset(index, encoded.data(), encoded.size(), tag);
}

bool store_reference_metadata(netnode &node, ea_t ea, uchar tag,
                              const std::vector<int64> &heap_types,
                              const std::vector<uchar> &ref_forms)
{
  bytevec encoded;
  if ( !encode_reference_metadata(&encoded, heap_types, ref_forms) )
    return false;
  return encoded.empty() || node.setblob_ea(encoded.data(), encoded.size(), ea, tag);
}

bool store_table_slots(netnode &node, const std::vector<table_slot_t> &slots)
{
  bytevec encoded;
  append_uleb(&encoded, slots.size());
  for ( const table_slot_t &slot : slots )
  {
    append_uleb(&encoded, slot.table_index);
    append_uleb(&encoded, slot.slot_index);
    append_uleb(&encoded, slot.function_index);
    append_uleb(&encoded, slot.function_type_index);
  }
  return node.setblob(encoded.data(), encoded.size(), 0, 'Z');
}

bool store_table_metadata(netnode &node, const module_t &module)
{
  const size_t count = module.table_types.size();
  if ( count > UINT32_MAX || module.table_minimums.size() != count
       || module.table_maximums.size() != count || module.table_has_maximum.size() != count
       || module.table64.size() != count )
    return false;
  const uint32 table_count = uint32(count);
  if ( !node.supset(0, &table_count, sizeof(table_count), 'x') )
    return false;
  for ( size_t i = 0; i < count; ++i )
  {
    const uint64 minimum = module.table_minimums[i];
    const bool has_maximum = module.table_has_maximum[i] != 0;
    const bool fixed = has_maximum && module.table_maximums[i] == minimum;
    const uchar flags
      = uchar((has_maximum ? 1 : 0) | (module.table64[i] != 0 ? 2 : 0) | (fixed ? 4 : 0));
    if ( !node.supset(uint32(i), &minimum, sizeof(minimum), 'y')
         || !node.supset(uint32(i), &flags, sizeof(flags), 'z') )
      return false;
  }
  return true;
}

bool build_global_storage(const module_t &module, bytevec *storage)
{
  if ( storage == nullptr || module.global_storage_size > max_module_size )
    return false;
  storage->resize(module.global_storage_size);
  if ( !storage->empty() )
    memset(storage->data(), 0, storage->size());
  for ( const global_t &global : module.globals )
  {
    const size_t width = global.type == 0x7E || global.type == 0x7C ? 8 : 4;
    if ( global.storage_offset > storage->size()
         || width > storage->size() - global.storage_offset )
      return false;
    for ( size_t i = 0; i < width; ++i )
      (*storage)[global.storage_offset + i] = uchar(global.initial_bits >> (i * 8));
  }
  return true;
}

bool store_global_metadata(netnode &node, uint32 index, const global_t &global, ea_t address)
{
  if ( address == BADADDR || !node.supset(index, &global.type, sizeof(global.type), 'g')
       || !node.supset(index, &global.mutability, sizeof(global.mutability), 'h')
       || !node.supset(index, &address, sizeof(address), 'j') )
    return false;
  if ( global.imported )
    return !global.import_helper.empty()
      && node.supset(index, global.import_helper.c_str(), global.import_helper.length() + 1, 'k');
  return true;
}
}
