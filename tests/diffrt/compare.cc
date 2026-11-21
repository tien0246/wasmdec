// Usage: compare <wasm_out> <native_out>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include <algorithm>

struct OutputLine
{
  std::string input_args;
  std::string result_type; // "i32"/"i64"/"f32"/"f64"/"TRAP"/"ERROR"
  std::string result_bits; // hex string
};

static OutputLine parse_line(const std::string &line)
{
  OutputLine out;
  // Find "OUT " or " TRAP" or " ERROR"
  size_t out_pos = line.find(" OUT ");
  size_t trap_pos = line.find(" TRAP");
  size_t err_pos = line.find(" ERROR");
  if ( trap_pos != std::string::npos )
  {
    out.input_args = line.substr(3, trap_pos - 3);
    out.result_type = "TRAP";
    return out;
  }
  if ( err_pos != std::string::npos )
  {
    out.input_args = line.substr(3, err_pos - 3);
    out.result_type = "ERROR";
    return out;
  }
  if ( out_pos == std::string::npos )
  {
    out.result_type = "SKIP";
    return out;
  }
  out.input_args = line.substr(3, out_pos - 3);
  std::string rest = line.substr(out_pos + 5);
  size_t colon = rest.find(':');
  if ( colon == std::string::npos )
  {
    out.result_type = "SKIP";
    return out;
  }
  out.result_type = rest.substr(0, colon);
  out.result_bits = rest.substr(colon + 1);
  // Trim trailing whitespace
  while ( !out.result_bits.empty()
          && (out.result_bits.back() == ' ' || out.result_bits.back() == '\n'
              || out.result_bits.back() == '\r') )
    out.result_bits.pop_back();
  return out;
}

static std::string canonicalize_nan(const std::string &type, const std::string &hex)
{
  if ( type == "f32" && hex.length() == 8 )
  {
    unsigned bits = strtoul(hex.c_str(), NULL, 16);
    unsigned exp = (bits >> 23) & 0xFF;
    unsigned mant = bits & 0x7FFFFF;
    if ( exp == 0xFF && mant != 0 )
      return "7fc00000";
  }
  else if ( type == "f64" && hex.length() == 16 )
  {
    unsigned long long bits = strtoull(hex.c_str(), NULL, 16);
    unsigned exp = (unsigned)((bits >> 52) & 0x7FF);
    unsigned long long mant = bits & 0xFFFFFFFFFFFFFULL;
    if ( exp == 0x7FF && mant != 0 )
      return "7ff8000000000000";
  }
  // Lowercase for consistent comparison
  std::string lower = hex;
  std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
  return lower;
}

static std::vector<std::string> read_lines(const char *path)
{
  FILE *f = fopen(path, "r");
  if ( !f )
  {
    fprintf(stderr, "cannot open %s\n", path);
    exit(1);
  }
  std::vector<std::string> lines;
  char buf[2048];
  while ( fgets(buf, sizeof(buf), f) )
  {
    std::string line(buf);
    while ( !line.empty() && (line.back() == '\n' || line.back() == '\r') )
      line.pop_back();
    if ( !line.empty() && line[0] == 'I' ) // starts with "IN"
      lines.push_back(line);
  }
  fclose(f);
  return lines;
}

int main(int argc, char **argv)
{
  if ( argc != 3 )
  {
    fprintf(stderr, "usage: %s <wasm_out> <native_out>\n", argv[0]);
    return 1;
  }
  auto wasm_lines = read_lines(argv[1]);
  auto native_lines = read_lines(argv[2]);

  if ( wasm_lines.size() != native_lines.size() )
  {
    printf("LINE_COUNT_MISMATCH wasm=%zu native=%zu\n", wasm_lines.size(), native_lines.size());
    return 1;
  }

  int mismatches = 0, total = 0;
  for ( size_t i = 0; i < wasm_lines.size(); i++ )
  {
    auto w = parse_line(wasm_lines[i]);
    auto n = parse_line(native_lines[i]);
    if ( w.result_type == "SKIP" || n.result_type == "SKIP" )
      continue;
    total++;
    if ( w.result_type == "TRAP" && n.result_type == "TRAP" )
      continue;
    if ( w.result_type == "TRAP" || n.result_type == "TRAP" )
    {
      printf("MISMATCH line %zu: %s wasm=%s native=%s\n", i + 1, w.input_args.c_str(),
             w.result_type.c_str(), n.result_type.c_str());
      mismatches++;
      continue;
    }
    if ( w.result_type != n.result_type )
    {
      printf("MISMATCH line %zu: type %s!=%s\n", i + 1, w.result_type.c_str(),
             n.result_type.c_str());
      mismatches++;
      continue;
    }
    auto wc = canonicalize_nan(w.result_type, w.result_bits);
    auto nc = canonicalize_nan(n.result_type, n.result_bits);
    if ( wc != nc )
    {
      printf("MISMATCH line %zu: %s %s wasm=%s native=%s\n", i + 1, w.input_args.c_str(),
             w.result_type.c_str(), w.result_bits.c_str(), n.result_bits.c_str());
      mismatches++;
    }
  }
  printf("COMPARE total=%d mismatches=%d\n", total, mismatches);
  return mismatches ? 1 : 0;
}
