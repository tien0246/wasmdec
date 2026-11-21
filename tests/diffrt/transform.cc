#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <regex>
#include <string>
#include <vector>

namespace
{

const char *const kHeader = R"(
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <inttypes.h>
#include <math.h>
#include "runtime_stubs.h"

// COERCE_* helpers: type punning without UB.
static inline double __c4_coerce_double(uint64_t b) { double d; memcpy(&d,&b,8); return d; }
static inline float  __c4_coerce_float(uint32_t b)  { float f;  memcpy(&f,&b,4); return f; }
static inline uint64_t __c4_coerce_u64(double d) { uint64_t b; memcpy(&b,&d,8); return b; }
static inline int64_t  __c4_coerce_i64(double d) { int64_t b;  memcpy(&b,&d,8); return b; }
static inline uint32_t __c4_coerce_u32(float f)  { uint32_t b; memcpy(&b,&f,4); return b; }
static inline uint64_t __c4_coerce_u64_from_int(uint32_t lo, uint32_t hi) {
    return ((uint64_t)hi << 32) | lo;
}
// HIDWORD/LODWORD: bit-level access, safe for both integers and floats.
#define HIDWORD(x) __extension__({ \
    __typeof__(x) __t = (x); \
    uint32_t __r; memcpy(&__r, (char*)&__t + 4, 4); __r; })
#define LODWORD(x) __extension__({ \
    __typeof__(x) __t = (x); \
    uint32_t __r; memcpy(&__r, &__t, 4); __r; })
// Write helpers for the "HIDWORD(x) = y" pattern.
static inline void __c4_set_hi32(void *dst, uint32_t val) {
    memcpy((char*)dst + 4, &val, 4);
}
static inline void __c4_set_lo32(void *dst, uint32_t val) {
    memcpy(dst, &val, 4);
}
// __SET_PAIR__ — we just evaluate and split.
#define __c4_set_pair_begin(lo, hi, expr) do { uint64_t __c4_v = (expr); (lo) = (uint32_t)__c4_v; (hi) = (uint32_t)(__c4_v >> 32); } while(0)
// Pseudocode "BOOL" → int
typedef int BOOL;

// Silence unused warnings for common Hex-Rays idioms.
#pragma GCC diagnostic ignored "-Wunused-variable"
#pragma GCC diagnostic ignored "-Wunused-but-set-variable"
)";

const char *const kMainTemplate = R"(
int main(int argc, char **argv) {
  if (argc != 2) { fprintf(stderr, "usage: %%s <inputs.txt>\n", argv[0]); return 1; }
  FILE *in = fopen(argv[1], "r");
  if (!in) { perror(argv[1]); return 1; }
  char line[1024];
  while (fgets(line, sizeof(line), in)) {
    if (line[0] == '#' || line[0] == '\n') continue;
    __C4_PARAMS__
    char *cursor = line;
    int consumed = 0;
    while (*cursor) {
      char type[8] = {0}, value[64] = {0};
      if (sscanf(cursor, " %7[^:]:%63s", type, value) == 2) {
        uint64_t raw = strtoull(value, NULL, type[0]=='f' ? 16 : 10);
        __C4_PARSE__
        consumed++;
      }
      char *sp = strchr(cursor, ' ');
      if (!sp) break;
      cursor = sp + 1;
    }
    printf("IN");
    __C4_PRINT_ARGS__
    fflush(stdout);
    __C4_CALL_AND_PRINT__
  }
  fclose(in);
  return 0;
}
)";

std::string strip_line_comments(const std::string &source)
{
  std::string out;
  out.reserve(source.size());
  size_t line_start = 0;
  while ( line_start <= source.size() )
  {
    const size_t nl = source.find('\n', line_start);
    const size_t line_end = (nl == std::string::npos) ? source.size() : nl;
    std::string line = source.substr(line_start, line_end - line_start);
    const size_t comment_pos = line.find("//");
    if ( comment_pos != std::string::npos )
    {
      size_t cut = comment_pos;
      while ( cut > 0 && (line[cut - 1] == ' ' || line[cut - 1] == '\t') )
        --cut;
      line.erase(cut);
    }
    out += line;
    if ( nl == std::string::npos )
      break;
    out.push_back('\n');
    line_start = nl + 1;
  }
  return out;
}

std::string transform_source(std::string source)
{
  source = std::regex_replace(source, std::regex(R"(__usercall\s+(\w+)@\S+?\()"), "$1(");
  source = std::regex_replace(source, std::regex(R"(@<[\w:]+>)"), "");
  source = std::regex_replace(source, std::regex(R"(@[\w:]+)"), "");
  source = std::regex_replace(source, std::regex(R"(\s+BYREF\b)"), "");
  source = strip_line_comments(source);
  source = std::regex_replace(source, std::regex(R"(COERCE_DOUBLE\(([^)]+)\))"),
                              "__c4_coerce_double($1)");
  source
    = std::regex_replace(source, std::regex(R"(COERCE_FLOAT\(([^)]+)\))"), "__c4_coerce_float($1)");
  source = std::regex_replace(source, std::regex(R"(COERCE_UNSIGNED_INT64\(([^)]+)\))"),
                              "__c4_coerce_u64($1)");
  source
    = std::regex_replace(source, std::regex(R"(COERCE_INT64\(([^)]+)\))"), "__c4_coerce_i64($1)");
  source = std::regex_replace(source, std::regex(R"(COERCE_UNSIGNED_INT\(([^)]+)\))"),
                              "__c4_coerce_u32($1)");
  source = std::regex_replace(source, std::regex(R"(__PAIR64__\(([^,]+),\s*([^)]+)\))"),
                              "((uint64_t)($1) << 32 | (uint32_t)($2))");
  source = std::regex_replace(source, std::regex(R"(HIDWORD\((\w+)\)\s*=\s*([^;]+);)"),
                              "__c4_set_hi32(&$1, $2);");
  source = std::regex_replace(source, std::regex(R"(LODWORD\((\w+)\)\s*=\s*([^;]+);)"),
                              "__c4_set_lo32(&$1, $2);");
  source = std::regex_replace(source, std::regex(R"(__SET_PAIR__\(\s*(\w+)\s*,\s*(\w+)\s*,\s*)"),
                              "__c4_set_pair_begin($1, $2, ");
  return source;
}

void replace_first(std::string &target, const std::string &needle, const std::string &replacement)
{
  const size_t pos = target.find(needle);
  if ( pos != std::string::npos )
    target.replace(pos, needle.size(), replacement);
}

std::string join_strings(const std::vector<std::string> &items, const std::string &sep)
{
  std::string out;
  for ( size_t i = 0; i < items.size(); ++i )
  {
    if ( i != 0 )
      out += sep;
    out += items[i];
  }
  return out;
}

std::string detect_return_type(const std::string &source, const std::string &function_name)
{
  std::smatch match;
  if ( std::regex_search(source, match, std::regex(R"(^(\w+)\s+__usercall)")) )
    return match[1].str();

  size_t line_start = 0;
  const std::regex line_re("^(\\w+)\\s+" + function_name);
  while ( line_start <= source.size() )
  {
    const size_t nl = source.find('\n', line_start);
    const size_t line_end = (nl == std::string::npos) ? source.size() : nl;
    const std::string line = source.substr(line_start, line_end - line_start);
    if ( std::regex_search(line, match, line_re) )
      return match[1].str();
    if ( nl == std::string::npos )
      break;
    line_start = nl + 1;
  }
  return "int";
}

int run_transform(int argc, char **argv)
{
  if ( argc < 3 )
  {
    std::fprintf(stderr, "usage: %s <golden.c> <function_name> [param_types...]\n", argv[0]);
    return 1;
  }
  const std::string golden_path = argv[1];
  const std::string function_name = argv[2];
  std::vector<std::string> param_types;
  for ( int i = 3; i < argc; ++i )
    param_types.emplace_back(argv[i]);

  std::ifstream in(golden_path, std::ios::binary);
  if ( !in )
  {
    std::perror(golden_path.c_str());
    return 1;
  }
  const std::string source((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
  const std::string body = transform_source(source);

  std::vector<std::string> params_decl;
  std::vector<std::string> parse_lines;
  std::vector<std::string> print_args;
  std::vector<std::string> call_args;

  for ( size_t i = 0; i < param_types.size(); ++i )
  {
    const std::string &type_name = param_types[i];
    const std::string arg = "arg" + std::to_string(i);
    const std::string idx = std::to_string(i);
    if ( type_name == "i32" )
    {
      params_decl.push_back("int32_t " + arg + " = 0;");
      std::string parse_line = "if (!strcmp(type,\"i32\") && consumed==";
      parse_line += idx;
      parse_line += ") ";
      parse_line += arg;
      parse_line += " = (int32_t)raw;";
      parse_lines.push_back(parse_line);
      print_args.push_back("printf(\" i32:%08x\", (uint32_t)" + arg + ");");
      call_args.push_back(arg);
    }
    else if ( type_name == "i64" )
    {
      params_decl.push_back("int64_t " + arg + " = 0;");
      std::string parse_line = "if (!strcmp(type,\"i64\") && consumed==";
      parse_line += idx;
      parse_line += ") ";
      parse_line += arg;
      parse_line += " = (int64_t)raw;";
      parse_lines.push_back(parse_line);
      print_args.push_back("printf(\" i64:%016\" PRIx64, (uint64_t)" + arg + ");");
      call_args.push_back(arg);
    }
    else if ( type_name == "f32" )
    {
      params_decl.push_back("float " + arg + " = 0.0f;");
      std::string parse_line = "if (!strcmp(type,\"f32\") && consumed==";
      parse_line += idx;
      parse_line += ") memcpy(&";
      parse_line += arg;
      parse_line += ",(uint32_t[]){(uint32_t)raw},4);";
      parse_lines.push_back(parse_line);
      print_args.push_back("{ uint32_t __b; memcpy(&__b,&" + arg
                           + ",4); printf(\" f32:%08x\", __b); }");
      call_args.push_back(arg);
    }
    else if ( type_name == "f64" )
    {
      params_decl.push_back("double " + arg + " = 0.0;");
      std::string parse_line = "if (!strcmp(type,\"f64\") && consumed==";
      parse_line += idx;
      parse_line += ") memcpy(&";
      parse_line += arg;
      parse_line += ",(uint64_t[]){raw},8);";
      parse_lines.push_back(parse_line);
      print_args.push_back("{ uint64_t __b; memcpy(&__b,&" + arg
                           + ",8); printf(\" f64:%016\" PRIx64, __b); }");
      call_args.push_back(arg);
    }
  }

  std::string main_code = kMainTemplate;
  replace_first(main_code, "__C4_PARAMS__", join_strings(params_decl, "\n    "));
  replace_first(main_code, "__C4_PARSE__", join_strings(parse_lines, "\n        "));
  replace_first(main_code, "__C4_PRINT_ARGS__",
                "\n    " + join_strings(print_args, "\n    ") + ";");

  const std::string ret_type = detect_return_type(source, function_name);
  const std::string joined_call_args = join_strings(call_args, ", ");
  std::string result_code;
  if ( ret_type.find("double") != std::string::npos || ret_type.find("float") != std::string::npos )
  {
    const std::string width = (ret_type.find("double") != std::string::npos) ? "f64" : "f32";
    result_code = "    ";
    result_code += ret_type;
    result_code += " __result = ";
    result_code += function_name;
    result_code += "(";
    result_code += joined_call_args;
    result_code += ");\n    uint64_t __b; memcpy(&__b, &__result, sizeof(__result));\n";
    result_code += "    printf(\" OUT ";
    result_code += width;
    result_code += ":%016\" PRIx64 \"\\n\", __b); fflush(stdout);";
  }
  else if ( ret_type.find("int64") != std::string::npos
            || ret_type.find("__int64") != std::string::npos )
  {
    result_code = "    ";
    result_code += ret_type;
    result_code += " __result = ";
    result_code += function_name;
    result_code += "(";
    result_code += joined_call_args;
    result_code
      += ");\n    printf(\" OUT i64:%016\" PRIx64 \"\\n\", (uint64_t)__result); fflush(stdout);";
  }
  else if ( ret_type.find("void") != std::string::npos )
  {
    result_code = "    ";
    result_code += function_name;
    result_code += "(";
    result_code += joined_call_args;
    result_code += ");\n    printf(\"\\n\"); fflush(stdout);";
  }
  else
  {
    result_code = "    ";
    result_code += ret_type;
    result_code += " __result = ";
    result_code += function_name;
    result_code += "(";
    result_code += joined_call_args;
    result_code += ");\n    printf(\" OUT i32:%08x\\n\", (uint32_t)__result); fflush(stdout);";
  }
  replace_first(main_code, "__C4_CALL_AND_PRINT__", result_code);

  std::printf("%s\n%s\n%s\n", kHeader, body.c_str(), main_code.c_str());
  return 0;
}

} // namespace

int main(int argc, char **argv)
{
  try
  {
    return run_transform(argc, argv);
  }
  catch ( const std::exception &ex )
  {
    std::fprintf(stderr, "transform error: %s\n", ex.what());
    return 1;
  }
}
