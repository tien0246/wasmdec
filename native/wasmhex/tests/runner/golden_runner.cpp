// Golden runner: a single C++ executable replacing the per-fixture
// checkers; test logic is data (fixtures + golden _metrics.json).
// Usage:
//   golden_runner <repo-root> <fixture-list> <scratch-dir> <idausr-dir>
//                 [--run-ungolden] [--fail-on-repair]
// Output: "PASS/FAIL <fixture>::<func> <reason>" + "TOTAL ..." and exit code.
#include <sys/wait.h>
#include <unistd.h>

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>
#include <vector>

namespace
{

struct golden_function_t
{
  std::string status;
  long lines = -1;
  long stack = -1;
  long jumpout = -1;
};

struct run_function_t
{
  std::string status;
  long lines = 0;
  long stack = 0;
  long jumpout = 0;
};

// Repair counting

constexpr size_t REPAIR_KIND_COUNT = 5;
const char *const REPAIR_KIND_NAMES[REPAIR_KIND_COUNT] = {
  "b1_clip", "b2_snap", "b3_trim", "b4_clamp", "b5_fallback",
};

struct repair_counts_t
{
  long kind[REPAIR_KIND_COUNT] = { 0, 0, 0, 0, 0 };

  long total() const
  {
    long sum = 0;
    for ( size_t i = 0; i < REPAIR_KIND_COUNT; ++i )
      sum += kind[i];
    return sum;
  }

  void add(const repair_counts_t &other)
  {
    for ( size_t i = 0; i < REPAIR_KIND_COUNT; ++i )
      kind[i] += other.kind[i];
  }
};

// Classify one log line against the exact msg() strings. Lines that are
// outright rejections/failures ("jump target ... rejected", "branch stub
// index at ...", every other WASMHEX_F5_UNSUPPORTED) are NOT repairs and
// return -1.
int classify_repair_line(const std::string &line)
{
  if ( line.find("outside function") != std::string::npos
       && line.find("clipped") != std::string::npos )
    return 0; // b1_clip (loader)
  if ( line.find("WASMHEX_F5_UNSUPPORTED") != std::string::npos )
  {
    if ( line.find("snapped to") != std::string::npos )
      return 1; // b2_snap (processor)
    if ( line.find("WASMHEX_TRIM") != std::string::npos )
      return 2; // b3_trim
    if ( line.find("beyond depths") != std::string::npos
         || line.find("beyond list") != std::string::npos )
      return 3; // b4_clamp (decompiler)
    if ( line.find("depths missing") != std::string::npos
         || line.find("assuming depth 0") != std::string::npos
         || line.find("depth metadata missing") != std::string::npos )
      return 4; // b5_fallback (decompiler)
  }
  return -1;
}

// Count repair lines in a whole log (ida.log aggregate).
repair_counts_t count_repairs_in_log(const std::string &text)
{
  repair_counts_t counts;
  size_t pos = 0;
  while ( pos <= text.size() )
  {
    size_t end = text.find('\n', pos);
    if ( end == std::string::npos )
      end = text.size();
    const int kind = classify_repair_line(text.substr(pos, end - pos));
    if ( kind >= 0 )
      ++counts.kind[kind];
    if ( end == text.size() )
      break;
    pos = end + 1;
  }
  return counts;
}

void scan_stdout_repairs(const std::string &text,
                         std::map<std::string, repair_counts_t> *per_function,
                         repair_counts_t *unattributed)
{
  bool seen_marker = false;
  bool in_pseudocode = false;
  repair_counts_t pending;
  size_t pos = 0;
  while ( pos <= text.size() )
  {
    size_t end = text.find('\n', pos);
    if ( end == std::string::npos )
      end = text.size();
    const std::string line = text.substr(pos, end - pos);
    if ( in_pseudocode )
    {
      if ( line.compare(0, 10, "ORACLE_END") == 0 )
        in_pseudocode = false;
    }
    else if ( line.compare(0, 12, "ORACLE_FUNC ") == 0 )
    {
      // "<i>/<total> <name> <status> ..."
      size_t name_at = line.find(' ', 12);
      if ( name_at != std::string::npos )
      {
        ++name_at;
        const size_t name_end = line.find(' ', name_at);
        const std::string name = line.substr(
          name_at, name_end == std::string::npos ? std::string::npos : name_end - name_at);
        if ( !name.empty() && seen_marker )
          (*per_function)[name].add(pending); // emitted while decompiling `name`
        else
          unattributed->add(pending); // before the first marker: load/analysis
      }
      pending = repair_counts_t();
      seen_marker = true;
    }
    else if ( line.compare(0, 13, "ORACLE_BEGIN") == 0 )
      in_pseudocode = true;
    else
    {
      const int kind = classify_repair_line(line);
      if ( kind >= 0 )
        ++pending.kind[kind];
    }
    if ( end == text.size() )
      break;
    pos = end + 1;
  }
  unattributed->add(pending); // after the last marker
}

struct fixture_repairs_t
{
  std::map<std::string, repair_counts_t> per_function;
  repair_counts_t unattributed; // load/analysis phase + trailing lines
  repair_counts_t aggregate;    // max(ida.log, stdout) per kind
};

std::string basename_sans_wasm(const std::string &path)
{
  size_t slash = path.find_last_of('/');
  std::string name = slash == std::string::npos ? path : path.substr(slash + 1);
  if ( name.size() >= 5 && name.compare(name.size() - 5, 5, ".wasm") == 0 )
    name.resize(name.size() - 5);
  return name;
}

bool file_exists(const std::string &path)
{
  std::FILE *f = std::fopen(path.c_str(), "rb");
  if ( f == nullptr )
    return false;
  std::fclose(f);
  return true;
}

std::string read_file(const std::string &path)
{
  std::FILE *f = std::fopen(path.c_str(), "rb");
  if ( f == nullptr )
    return std::string();
  std::string out;
  char buffer[4096];
  size_t n;
  while ( (n = std::fread(buffer, 1, sizeof(buffer), f)) > 0 )
    out.append(buffer, n);
  std::fclose(f);
  return out;
}

bool copy_file(const std::string &from, const std::string &to)
{
  const std::string data = read_file(from);
  if ( data.empty() )
    return false;
  std::FILE *f = std::fopen(to.c_str(), "wb");
  if ( f == nullptr )
    return false;
  const bool ok = std::fwrite(data.data(), 1, data.size(), f) == data.size();
  std::fclose(f);
  return ok;
}

size_t skip_ws(const std::string &s, size_t i)
{
  while ( i < s.size() && (s[i] == ' ' || s[i] == '\n' || s[i] == '\r' || s[i] == '\t') )
    ++i;
  return i;
}

size_t parse_string(const std::string &s, size_t i, std::string *out)
{
  i = skip_ws(s, i);
  if ( i >= s.size() || s[i] != '"' )
    return std::string::npos;
  ++i;
  out->clear();
  while ( i < s.size() && s[i] != '"' )
  {
    if ( s[i] == '\\' && i + 1 < s.size() )
      ++i;
    out->push_back(s[i]);
    ++i;
  }
  return i < s.size() ? i + 1 : std::string::npos;
}

size_t parse_number(const std::string &s, size_t i, long *out)
{
  i = skip_ws(s, i);
  const size_t start = i;
  if ( i < s.size() && (s[i] == '-' || s[i] == '+') )
    ++i;
  while ( i < s.size() && s[i] >= '0' && s[i] <= '9' )
    ++i;
  if ( i == start )
    return std::string::npos;
  *out = std::strtol(s.substr(start, i - start).c_str(), nullptr, 10);
  return i;
}

// Balanced-brace extraction of the "functions": { ... } object, then per
// entry "<name>": { status/lines/stack/jumpout }.
bool parse_metrics(const std::string &json, std::map<std::string, golden_function_t> *functions)
{
  const size_t key_at = json.find("\"functions\"");
  if ( key_at == std::string::npos )
    return false;
  const size_t open = json.find('{', key_at);
  if ( open == std::string::npos )
    return false;
  int depth = 0;
  size_t close = std::string::npos;
  for ( size_t i = open; i < json.size(); ++i )
  {
    if ( json[i] == '{' )
      ++depth;
    else if ( json[i] == '}' )
    {
      if ( --depth == 0 )
      {
        close = i;
        break;
      }
    }
  }
  if ( close == std::string::npos )
    return false;
  const std::string block = json.substr(open + 1, close - open - 1);

  size_t pos = 0;
  while ( pos < block.size() )
  {
    const size_t name_start = block.find('"', pos);
    if ( name_start == std::string::npos )
      break;
    std::string name;
    const size_t after_name = parse_string(block, name_start, &name);
    if ( after_name == std::string::npos )
      return false;
    const size_t colon = block.find(':', after_name);
    if ( colon == std::string::npos )
      return false;
    const size_t entry_open = block.find('{', colon);
    if ( entry_open == std::string::npos )
      return false;
    int entry_depth = 0;
    size_t entry_close = std::string::npos;
    for ( size_t i = entry_open; i < block.size(); ++i )
    {
      if ( block[i] == '{' )
        ++entry_depth;
      else if ( block[i] == '}' )
      {
        if ( --entry_depth == 0 )
        {
          entry_close = i;
          break;
        }
      }
    }
    if ( entry_close == std::string::npos )
      return false;
    const std::string entry = block.substr(entry_open + 1, entry_close - entry_open - 1);
    golden_function_t fn;
    std::string value;
    const size_t status_key = entry.find("\"status\"");
    const size_t status_colon
      = status_key == std::string::npos ? std::string::npos : entry.find(':', status_key);
    if ( status_colon != std::string::npos
         && parse_string(entry, status_colon + 1, &value) != std::string::npos )
      fn.status = value;
    long number = 0;
    struct
    {
      const char *key;
      long *target;
    } scalars[] = {
      { "lines", &fn.lines },
      { "stack", &fn.stack },
      { "jumpout", &fn.jumpout },
    };
    for ( const auto &scalar : scalars )
    {
      const size_t at = entry.find(std::string("\"") + scalar.key + "\"");
      if ( at == std::string::npos )
        continue;
      const size_t colon = entry.find(':', at);
      if ( colon == std::string::npos )
        continue;
      if ( parse_number(entry, colon + 1, &number) != std::string::npos )
        *scalar.target = number;
    }
    (*functions)[name] = fn;
    pos = entry_close + 1;
  }
  return !functions->empty();
}

// Parse "ORACLE_FUNC i/total <name> <status> <ms> <lines> <stack> <jumpout>"
// markers from the worker output.
void parse_worker_output(const std::string &output, std::map<std::string, run_function_t> *ran)
{
  size_t pos = 0;
  while ( (pos = output.find("ORACLE_FUNC ", pos)) != std::string::npos )
  {
    pos += std::strlen("ORACLE_FUNC ");
    const size_t end = output.find('\n', pos);
    const std::string line
      = output.substr(pos, end == std::string::npos ? std::string::npos : end - pos);
    // line := "<i>/<total> <name> <status> <ms> <lines> <stack> <jumpout>"
    const size_t slash_at = line.find(' ');
    if ( slash_at == std::string::npos )
      break;
    const std::string rest = line.substr(slash_at + 1);
    std::vector<std::string> fields;
    size_t fpos = 0;
    while ( fields.size() < 6 )
    {
      const size_t space = rest.find(' ', fpos);
      if ( space == std::string::npos )
      {
        fields.push_back(rest.substr(fpos));
        break;
      }
      fields.push_back(rest.substr(fpos, space - fpos));
      fpos = space + 1;
    }
    if ( fields.size() >= 6 )
    {
      run_function_t fn;
      fn.status = fields[1];
      fn.lines = std::strtol(fields[3].c_str(), nullptr, 10);
      fn.stack = std::strtol(fields[4].c_str(), nullptr, 10);
      fn.jumpout = std::strtol(fields[5].c_str(), nullptr, 10);
      (*ran)[fields[0]] = fn;
    }
    pos = end == std::string::npos ? output.size() : end;
  }
}

} // anonymous namespace

int main(int argc, char **argv)
{
  if ( argc < 5 )
  {
    std::fprintf(stderr,
                 "usage: %s <repo-root> <fixture-list> <scratch-dir> "
                 "<idausr-dir> [--worker <worker.py>] [--run-ungolden] "
                 "[--fail-on-repair]\n"
                 "deprecated env: GOLDEN_WORKER=<worker.py> still overrides "
                 "the oracle script (one release, then removal)\n",
                 argv[0]);
    return 2;
  }
  const std::string root = argv[1];
  const std::string list_path = argv[2];
  const std::string scratch = argv[3];
  const std::string idausr_dir = argv[4];
  // idat: the WASMDEC_IDAT env var wins, else the configure-time default
  // baked in from WASMHEX_IDA_BUNDLE_MACOS_DIR (native/wasmhex/tests/runner/
  // CMakeLists.txt). Never a hardcoded absolute path.
  const char *env_idat = std::getenv("WASMDEC_IDAT");
  const std::string idat = (env_idat != nullptr && *env_idat != '\0')
    ? std::string(env_idat)
    : std::string(WASMDEC_IDAT_DEFAULT);
  if ( idat.empty() )
  {
    std::fprintf(stderr,
                 "golden_runner: no idat found — set WASMDEC_IDAT=<path to idat> "
                 "or configure with -DWASMHEX_IDA_BUNDLE_MACOS_DIR=<IDA.app/Contents/MacOS>\n");
    return 2;
  }
  std::string worker;
  const char *worker_env = std::getenv("GOLDEN_WORKER"); // deprecated alias
  if ( worker_env != nullptr && *worker_env != '\0' )
  {
    std::fprintf(stderr, "warning: GOLDEN_WORKER is deprecated — use --worker\n");
    worker = worker_env;
  }

  bool run_ungolden = false;
  bool fail_on_repair = false;
  for ( int i = 5; i < argc; ++i )
  {
    if ( std::strcmp(argv[i], "--run-ungolden") == 0 )
      run_ungolden = true;
    else if ( std::strcmp(argv[i], "--fail-on-repair") == 0 )
      fail_on_repair = true;
    else if ( std::strcmp(argv[i], "--worker") == 0 && i + 1 < argc )
      worker = argv[++i];
    else
      std::fprintf(stderr, "warning: unknown flag %s\n", argv[i]);
  }
  if ( worker.empty() )
    worker = root + "/tests/golden/oracle_ida.py";

  const std::string list_data = read_file(list_path);
  if ( list_data.empty() )
  {
    std::fprintf(stderr, "cannot read %s\n", list_path.c_str());
    return 2;
  }
  std::vector<std::string> fixtures;
  for ( size_t pos = 0; pos < list_data.size(); )
  {
    size_t end = list_data.find('\n', pos);
    if ( end == std::string::npos )
      end = list_data.size();
    std::string name = list_data.substr(pos, end - pos);
    while ( !name.empty() && (name.back() == '\r' || name.back() == ' ') )
      name.pop_back();
    if ( !name.empty() )
      fixtures.push_back(name);
    pos = end + 1;
  }

  int exit_code = 0;
  size_t pass = 0, fail = 0, skipped = 0;

  // Global accumulators across all fixtures.
  repair_counts_t global_repairs;
  repair_counts_t global_attributed;
  repair_counts_t global_unattributed;
  size_t global_ok = 0;
  size_t global_ok_with_repair = 0;
  bool attribution_echo_missing = false;

  for ( const std::string &fixture : fixtures )
  {
    // Entries containing '/' are .wasm paths (corpus entries, e.g. the
    // duckdb sample); plain names keep the golden-fixture resolution.
    const bool is_path_entry = fixture.find('/') != std::string::npos;
    const std::string fixture_name = is_path_entry ? basename_sans_wasm(fixture) : fixture;

    const std::string metrics_json
      = read_file(root + "/tests/golden/" + fixture_name + "/_metrics.json");
    std::map<std::string, golden_function_t> golden;
    const bool have_golden = !metrics_json.empty() && parse_metrics(metrics_json, &golden);
    if ( !have_golden && !run_ungolden )
    {
      if ( metrics_json.empty() )
        std::printf("SKIP %s (no _metrics.json)\n", fixture_name.c_str());
      else
        std::printf("SKIP %s (unparseable metrics)\n", fixture_name.c_str());
      ++skipped;
      continue;
    }

    std::string wasm_path;
    if ( is_path_entry )
    {
      if ( file_exists(fixture) )
        wasm_path = fixture;
    }
    else
    {
      for ( const char *dir : { "/tests/fixtures", "/tests/fixtures/brtable" } )
      {
        const std::string candidate = root + dir + "/" + fixture + ".wasm";
        if ( !read_file(candidate).empty() )
        {
          wasm_path = candidate;
          break;
        }
      }
    }
    if ( wasm_path.empty() )
    {
      std::printf("SKIP %s (fixture wasm missing)\n", fixture_name.c_str());
      ++skipped;
      continue;
    }

    const std::string work = scratch + "/" + fixture_name;
    const std::string mkdir = "mkdir -p '" + work + "'";
    if ( std::system(mkdir.c_str()) != 0 )
    {
      std::printf("FAIL %s :: scratch mkdir\n", fixture_name.c_str());
      ++fail;
      exit_code = 1;
      continue;
    }
    const std::string local = work + "/" + fixture_name + ".wasm";
    if ( !copy_file(wasm_path, local) )
    {
      std::printf("FAIL %s :: copy fixture\n", fixture_name.c_str());
      ++fail;
      exit_code = 1;
      continue;
    }

    const std::string log_path = work + "/ida.log";
    // Append the user's own ~/.idapro (license / ida.reg) behind the isolated
    // dir when HOME exists — no hardcoded home path.
    std::string idausr_env = "IDAUSR=" + idausr_dir;
    if ( const char *home_dir = std::getenv("HOME"); home_dir != nullptr && *home_dir != '\0' )
      idausr_env += std::string(":") + home_dir + "/.idapro";
    const std::string command = idausr_env + " '" + idat
      + "' -c -A -pwasmdec '-TWebAssembly Decompiler' -o" + work + "/test.i64 -L" + log_path + " -S"
      + worker + " '" + local + "' > " + work + "/stdout.txt 2>&1";
    const int rc = std::system(command.c_str());
    const std::string log_text = read_file(log_path);
    const std::string stdout_text = read_file(work + "/stdout.txt");
    const std::string output = log_text + "\n" + stdout_text;

    std::map<std::string, run_function_t> ran;
    parse_worker_output(output, &ran);

    fixture_repairs_t repairs;
    const bool log_has_markers = log_text.find("ORACLE_FUNC ") != std::string::npos;
    const std::string &attribution_text = log_has_markers ? log_text : stdout_text;
    scan_stdout_repairs(attribution_text, &repairs.per_function, &repairs.unattributed);
    const repair_counts_t log_counts = count_repairs_in_log(log_text);
    repair_counts_t attributed_totals;
    for ( const auto &pair : repairs.per_function )
      attributed_totals.add(pair.second);
    attributed_totals.add(repairs.unattributed);
    for ( size_t i = 0; i < REPAIR_KIND_COUNT; ++i )
      repairs.aggregate.kind[i] = log_counts.kind[i] > attributed_totals.kind[i]
        ? log_counts.kind[i]
        : attributed_totals.kind[i];
    if ( log_counts.total() > 0 && !log_has_markers
         && stdout_text.find("WASMHEX_F5_UNSUPPORTED") == std::string::npos
         && stdout_text.find("outside function") == std::string::npos )
      attribution_echo_missing = true;

    size_t funcs_with_repair = 0;
    size_t ok_funcs = 0;
    size_t ok_funcs_with_repair = 0;
    for ( const auto &pair : repairs.per_function )
    {
      if ( pair.second.total() == 0 )
        continue;
      ++funcs_with_repair;
      const auto it = ran.find(pair.first);
      if ( it != ran.end() && it->second.status == "OK" )
        ++ok_funcs_with_repair;
    }
    for ( const auto &pair : ran )
      if ( pair.second.status == "OK" )
        ++ok_funcs;

    for ( const auto &pair : repairs.per_function )
      for ( size_t i = 0; i < REPAIR_KIND_COUNT; ++i )
        if ( pair.second.kind[i] > 0 )
          std::printf("REPAIR_METRICS %s :: %s %s %ld\n", fixture_name.c_str(), pair.first.c_str(),
                      REPAIR_KIND_NAMES[i], pair.second.kind[i]);
    for ( size_t i = 0; i < REPAIR_KIND_COUNT; ++i )
      if ( repairs.unattributed.kind[i] > 0 )
        std::printf("REPAIR_METRICS %s :: (load-analysis) %s %ld\n", fixture_name.c_str(),
                    REPAIR_KIND_NAMES[i], repairs.unattributed.kind[i]);
    long attributed_total = 0;
    for ( const auto &pair : repairs.per_function )
      attributed_total += pair.second.total();
    const long scanner_unattributed = repairs.unattributed.total();
    const long log_only_total = repairs.aggregate.total() - attributed_total - scanner_unattributed;
    {
      const size_t ok_zero = ok_funcs >= ok_funcs_with_repair ? ok_funcs - ok_funcs_with_repair : 0;
      std::printf("REPAIR_SUMMARY %s b1_clip=%ld b2_snap=%ld b4_clamp=%ld b5_fallback=%ld "
                  "attributed=%ld unattributed=%ld log_only=%ld funcs_with_repair=%zu ok=%zu "
                  "ok_zero_repair=%zu/%zu\n",
                  fixture_name.c_str(), repairs.aggregate.kind[0], repairs.aggregate.kind[1],
                  repairs.aggregate.kind[2], repairs.aggregate.kind[3], attributed_total,
                  scanner_unattributed, log_only_total > 0 ? log_only_total : 0, funcs_with_repair,
                  ok_funcs, ok_zero, ok_funcs);
    }
    global_repairs.add(repairs.aggregate);
    for ( const auto &pair : repairs.per_function )
      global_attributed.add(pair.second);
    global_unattributed.add(repairs.unattributed);
    global_ok += ok_funcs;
    global_ok_with_repair += ok_funcs_with_repair;

    if ( ran.empty() )
    {
      std::printf("FAIL %s :: worker produced no ORACLE_FUNC markers (rc=%d)\n",
                  fixture_name.c_str(), rc);
      ++fail;
      exit_code = 1;
      continue;
    }
    if ( !have_golden )
    {
      std::printf("NOMETRICS %s (repair metrics only)\n", fixture_name.c_str());
      continue;
    }

    for ( const auto &pair : golden )
    {
      const std::string &name = pair.first;
      const golden_function_t &expected = pair.second;
      const auto it = ran.find(name);
      if ( it == ran.end() )
      {
        std::printf("FAIL %s :: %s missing from run\n", fixture_name.c_str(), name.c_str());
        ++fail;
        exit_code = 1;
        continue;
      }
      const run_function_t &actual = it->second;
      const char *verdict = "PASS";
      std::string reason;
      if ( actual.status != expected.status )
      {
        verdict = "FAIL";
        reason = "status " + actual.status + " != " + expected.status;
      }
      else if ( expected.status == "OK" )
      {
        if ( expected.lines >= 0 && actual.lines != expected.lines )
        {
          verdict = "FAIL";
          reason
            = "lines " + std::to_string(actual.lines) + " != " + std::to_string(expected.lines);
        }
        else if ( expected.stack >= 0 && actual.stack != expected.stack )
        {
          verdict = "FAIL";
          reason
            = "stack " + std::to_string(actual.stack) + " != " + std::to_string(expected.stack);
        }
        else if ( expected.jumpout >= 0 && actual.jumpout != expected.jumpout )
        {
          verdict = "FAIL";
          reason = "jumpout " + std::to_string(actual.jumpout)
            + " != " + std::to_string(expected.jumpout);
        }
      }
      std::printf("%s %s :: %s %s\n", verdict, fixture_name.c_str(), name.c_str(), reason.c_str());
      if ( verdict[0] == 'P' )
        ++pass;
      else
      {
        ++fail;
        exit_code = 1;
      }
    }
  }
  std::printf("TOTAL pass=%zu fail=%zu skip=%zu\n", pass, fail, skipped);

  // Honest-repair totals (after TOTAL: purely additive output).
  std::printf("REPAIR_TOTAL b1_clip=%ld b2_snap=%ld b4_clamp=%ld b5_fallback=%ld total=%ld "
              "attributed=%ld unattributed=%ld log_only=%ld\n",
              global_repairs.kind[0], global_repairs.kind[1], global_repairs.kind[2],
              global_repairs.kind[3], global_repairs.total(), global_attributed.total(),
              global_unattributed.total(),
              global_repairs.total() - global_attributed.total() - global_unattributed.total() > 0
                ? global_repairs.total() - global_attributed.total() - global_unattributed.total()
                : 0);
  {
    const size_t ok_zero
      = global_ok >= global_ok_with_repair ? global_ok - global_ok_with_repair : 0;
    const double pct = global_ok > 0 ? 100.0 * double(ok_zero) / double(global_ok) : 0.0;
    std::printf("REPAIR_FUNCS ok=%zu ok_with_repair=%zu ok_zero_repair=%zu/%zu (%.1f%%)\n",
                global_ok, global_ok_with_repair, ok_zero, global_ok, pct);
  }
  std::printf("REPAIR_NOTE b3_trim: S5 counts WASMHEX_TRIM fail-closed lines "
              "(surplus stack at reachable block-end)\n");
  std::printf("REPAIR_NOTE b6_crossfunc: Go cross-function branch skips are not logged - "
              "uncountable this round\n");
  std::printf("REPAIR_NOTE b4_clamp: index==0 clamps stay silent (lower_control.cpp) - "
              "b4 counts are a lower bound\n");
  if ( attribution_echo_missing )
    std::printf("REPAIR_NOTE attribution: worker does not msg() ORACLE markers into ida.log "
                "and idat does not echo msg() to stdout; per-function counts are 0 by "
                "construction, aggregate comes from ida.log\n");
  if ( fail_on_repair )
  {
    // Phase-2 CI gate: any counted repair fails the run. Kept OFF until
    // the underlying fixes land; this round only baselines.
    if ( global_repairs.total() > 0 )
    {
      std::printf("REPAIR_GATE failed: %ld repairs > 0 (--fail-on-repair)\n",
                  global_repairs.total());
      exit_code = 1;
    }
    else
      std::printf("REPAIR_GATE ok: 0 repairs\n");
  }
  return exit_code;
}
