#define USE_STANDARD_FILE_FUNCTIONS 1

#include <idalib.hpp>
#include <hexrays.hpp>
#include <ida.hpp>
#include <idp.hpp>
#include <funcs.hpp>
#include <name.hpp>
#include <auto.hpp>
#include <lines.hpp>
#include <loader.hpp>
#include <kernwin.hpp>
#include <bytes.hpp>
#include <netnode.hpp>
#include <ua.hpp>
#include <typeinf.hpp>
#include <nalt.hpp>
#include <segment.hpp>
#include <xref.hpp>
#include <demangle.hpp>
#include <pro.h>

#include "wasmtime_eval.hpp"

#include <CommonCrypto/CommonDigest.h>
#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <map>
#include <regex>
#include <set>
#include <sstream>
#include <string>
#include <vector>

extern "C" __attribute__((used)) const char wasmdec_m6_s2_runner_build_marker[]
  = "WASMDEC_BUILD_MARKER_M6_S2_CPP_IDALIB_RUNNER";

extern "C" __attribute__((used)) const char wasmdec_m6_s4_runner_build_marker[]
  = "WASMDEC_BUILD_MARKER_M6_S4_ALL_CPP_RUNNER";

// Phase 2 branding: the binary is wasmdec_runner and stages the wasmdec /
// wasmdecldr dylibs; log prefixes (WASMHEX_F5_* / F5_SUBSET_SHA256) keep the
// historical wasmhex spelling so every pinned sweep hash stays comparable.
extern "C" __attribute__((used)) const char wasmdec_phase2_brand_marker[]
  = "WASMDEC_BUILD_MARKER_PHASE2_BRAND_RENAME";

namespace fs = std::filesystem;

namespace
{

// Utility helpers

std::string read_file_text(const fs::path &p)
{
  std::ifstream ifs(p, std::ios::in | std::ios::binary);
  if ( !ifs )
    return std::string();
  std::ostringstream oss;
  oss << ifs.rdbuf();
  return oss.str();
}

bool write_file_text(const fs::path &p, const std::string &content)
{
  std::ofstream ofs(p, std::ios::out | std::ios::binary | std::ios::trunc);
  if ( !ofs )
    return false;
  ofs.write(content.data(), static_cast<std::streamsize>(content.size()));
  return ofs.good();
}

std::vector<std::string> split_lines_no_eol(const std::string &text)
{
  std::vector<std::string> lines;
  size_t pos = 0;
  while ( pos < text.size() )
  {
    size_t nl = text.find('\n', pos);
    if ( nl == std::string::npos )
    {
      std::string line = text.substr(pos);
      if ( !line.empty() && line.back() == '\r' )
        line.pop_back();
      lines.push_back(std::move(line));
      break;
    }
    std::string line = text.substr(pos, nl - pos);
    if ( !line.empty() && line.back() == '\r' )
      line.pop_back();
    lines.push_back(std::move(line));
    pos = nl + 1;
  }
  return lines;
}

std::string trim_ws(const std::string &s)
{
  size_t b = 0;
  while ( b < s.size() && (s[b] == ' ' || s[b] == '\t' || s[b] == '\r' || s[b] == '\n') )
    ++b;
  size_t e = s.size();
  while ( e > b && (s[e - 1] == ' ' || s[e - 1] == '\t' || s[e - 1] == '\r' || s[e - 1] == '\n') )
    --e;
  return s.substr(b, e - b);
}

std::string sha256_hex(const std::string &data)
{
  unsigned char digest[CC_SHA256_DIGEST_LENGTH];
  CC_SHA256(data.data(), static_cast<CC_LONG>(data.size()), digest);
  char hex[CC_SHA256_DIGEST_LENGTH * 2 + 1];
  for ( size_t i = 0; i < CC_SHA256_DIGEST_LENGTH; ++i )
    qsnprintf(hex + i * 2, 3, "%02x", digest[i]);
  hex[CC_SHA256_DIGEST_LENGTH * 2] = '\0';
  return std::string(hex);
}

size_t count_substr(const std::string &haystack, const std::string &needle)
{
  if ( needle.empty() )
    return 0;
  size_t count = 0;
  size_t pos = 0;
  while ( (pos = haystack.find(needle, pos)) != std::string::npos )
  {
    ++count;
    pos += needle.size();
  }
  return count;
}

std::string safe_func_name(const std::string &name)
{
  std::string cleaned;
  cleaned.reserve(name.size());
  for ( unsigned char c : name )
  {
    if ( (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_'
         || c == '.' || c == '-' )
      cleaned.push_back(static_cast<char>(c));
    else
      cleaned.push_back('_');
  }
  if ( cleaned.size() > 180 )
    cleaned.resize(180);
  if ( cleaned.empty() )
    return "unnamed";
  return cleaned;
}

std::string json_escape(const std::string &s)
{
  std::string out;
  out.reserve(s.size() + 8);
  for ( unsigned char c : s )
  {
    switch ( c )
    {
      case '"': out += "\\\""; break;
      case '\\': out += "\\\\"; break;
      case '\b': out += "\\b"; break;
      case '\f': out += "\\f"; break;
      case '\n': out += "\\n"; break;
      case '\r': out += "\\r"; break;
      case '\t': out += "\\t"; break;
      default:
        if ( c < 0x20 )
        {
          char buf[8];
          qsnprintf(buf, sizeof(buf), "\\u%04x", unsigned(c));
          out += buf;
        }
        else
        {
          out.push_back(static_cast<char>(c));
        }
        break;
    }
  }
  return out;
}

std::string iso_timestamp_seconds()
{
  std::time_t now = std::time(nullptr);
  std::tm tm_val{};
  localtime_r(&now, &tm_val);
  char buf[32];
  std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%S", &tm_val);
  return std::string(buf);
}

// Unconditional verify mode + isolated IDAUSR staging

std::string read_env_str(const char *name)
{
  qstring qv;
  if ( qgetenv(name, &qv) && !qv.empty() )
    return std::string(qv.c_str());
  return std::string();
}

void force_enable_verify_env()
{
  std::string val = read_env_str("WASMHEX_DEBUG");
  if ( val.empty() )
  {
    qsetenv("WASMHEX_DEBUG", "verify");
    return;
  }
  // Check if "verify" token is already present
  size_t pos = 0;
  while ( pos <= val.size() )
  {
    size_t comma = val.find(',', pos);
    std::string tok = val.substr(pos, comma == std::string::npos ? std::string::npos : comma - pos);
    if ( tok == "verify" )
      return;
    if ( comma == std::string::npos )
      break;
    pos = comma + 1;
  }
  std::string updated = "verify," + val;
  qsetenv("WASMHEX_DEBUG", updated.c_str());
}

bool has_debug_token(const char *token)
{
  std::string val = read_env_str("WASMHEX_DEBUG");
  if ( val.empty() )
    return false;
  size_t pos = 0;
  while ( pos <= val.size() )
  {
    size_t comma = val.find(',', pos);
    std::string tok = val.substr(pos, comma == std::string::npos ? std::string::npos : comma - pos);
    if ( tok == token )
      return true;
    if ( comma == std::string::npos )
      break;
    pos = comma + 1;
  }
  return false;
}

fs::path find_repo_root()
{
  std::error_code ec;
  fs::path cwd = fs::current_path(ec);
  for ( fs::path p = cwd; !p.empty() && p != p.root_path(); p = p.parent_path() )
  {
    if ( fs::exists(p / "tests" / "golden", ec) && fs::exists(p / "native" / "wasmhex", ec) )
      return p;
  }
  // No marker directory found walking up: fall back to the current directory
  // (CMake/CI invocations run from the repo root). Never a hardcoded path —
  // downstream resolution simply fails with "not found" if this is wrong.
  std::fprintf(stderr, "wasmdec_runner: repo root not found above %s; using it as the root\n",
               cwd.string().c_str());
  return cwd.empty() ? fs::path(".") : cwd;
}

struct staged_idausr_t
{
  fs::path dir;
  bool owns_dir = false;

  ~staged_idausr_t()
  {
    if ( owns_dir && !dir.empty() )
    {
      std::error_code ec;
      fs::remove_all(dir, ec);
    }
  }
};

bool setup_isolated_idausr(const fs::path &explicit_idausr, staged_idausr_t *out)
{
  std::error_code ec;
  std::string home_env = read_env_str("HOME");
  // No HOME -> no user .idapro to fall back to (empty path; every consumer
  // below is guarded on !home_idapro.empty()).
  fs::path home_idapro = !home_env.empty() ? (fs::path(home_env) / ".idapro") : fs::path();

  fs::path src_procs;
  fs::path src_loaders;
  if ( !explicit_idausr.empty() )
  {
    if ( fs::exists(explicit_idausr / "procs" / "wasmdec.dylib", ec) )
      src_procs = explicit_idausr / "procs" / "wasmdec.dylib";
    else if ( fs::exists(explicit_idausr / "procs" / "wasm.dylib", ec) )
      src_procs = explicit_idausr / "procs" / "wasm.dylib";
    if ( fs::exists(explicit_idausr / "loaders" / "wasmdecldr.dylib", ec) )
      src_loaders = explicit_idausr / "loaders" / "wasmdecldr.dylib";
  }
  else
  {
    std::string env_idausr = read_env_str("IDAUSR");
    if ( !env_idausr.empty() )
    {
      size_t colon = env_idausr.find(':');
      fs::path first = env_idausr.substr(0, colon);
      if ( fs::exists(first / "procs" / "wasmdec.dylib", ec)
           && fs::exists(first / "loaders" / "wasmdecldr.dylib", ec)
           && !fs::exists(first / "plugins", ec) && fs::exists(first / "ida.reg", ec) )
      {
        // Already an isolated staged IDAUSR directory with ida.reg
        qsetenv("IDAUSR", first.c_str());
        out->dir = first;
        out->owns_dir = false;
        return true;
      }
      if ( fs::exists(first / "procs" / "wasmdec.dylib", ec) )
        src_procs = first / "procs" / "wasmdec.dylib";
      if ( fs::exists(first / "loaders" / "wasmdecldr.dylib", ec) )
        src_loaders = first / "loaders" / "wasmdecldr.dylib";
    }
    if ( src_procs.empty() && !home_idapro.empty()
         && fs::exists(home_idapro / "procs" / "wasmdec.dylib", ec) )
      src_procs = home_idapro / "procs" / "wasmdec.dylib";
    if ( src_loaders.empty() && !home_idapro.empty()
         && fs::exists(home_idapro / "loaders" / "wasmdecldr.dylib", ec) )
      src_loaders = home_idapro / "loaders" / "wasmdecldr.dylib";
  }

  if ( src_procs.empty() || src_loaders.empty() )
  {
    std::fprintf(stderr, "wasmdec_runner: cannot locate wasmdec.dylib / wasmdecldr.dylib\n");
    return false;
  }

  char tmpl[] = "/private/tmp/wasmdec_runner_idausr_XXXXXX";
  char *made = mkdtemp(tmpl);
  if ( made == nullptr )
  {
    std::fprintf(stderr, "wasmdec_runner: mkdtemp failed for IDAUSR staging\n");
    return false;
  }
  fs::path staged(made);
  out->dir = staged;
  out->owns_dir = true;

  fs::create_directories(staged / "procs", ec);
  fs::create_directories(staged / "loaders", ec);
  fs::copy_file(src_procs, staged / "procs" / "wasmdec.dylib", fs::copy_options::overwrite_existing,
                ec);
  fs::copy_file(src_loaders, staged / "loaders" / "wasmdecldr.dylib",
                fs::copy_options::overwrite_existing, ec);
  if ( !home_idapro.empty() && fs::exists(home_idapro / "ida.reg", ec) )
  {
    fs::copy_file(home_idapro / "ida.reg", staged / "ida.reg", fs::copy_options::overwrite_existing,
                  ec);
  }
  if ( !home_idapro.empty() && fs::exists(home_idapro / "ida.hexlic", ec) )
  {
    fs::copy_file(home_idapro / "ida.hexlic", staged / "ida.hexlic",
                  fs::copy_options::overwrite_existing, ec);
  }

  qsetenv("IDAUSR", staged.c_str());
  return true;
}

// During loader_t::load_file, set_processor_type("wasmdec") loads a
// PR_DEFSEG32 processor and IDA's kernel resets LFLG_64BIT to 32-bit right
// before the loader's set_compiler call; when the compiler_changed event
// fires with a 64-bit pointer model, restore the 64-bit application
// bitness so the above-4GiB segments in load_file succeed.
class idalib_idb_listener_t final : public event_listener_t
{
public:
  ssize_t idaapi on_event(ssize_t event, va_list) override
  {
    if ( event == idb_event::local_types_changed || event == idb_event::compiler_changed )
    {
      if ( (inf_get_cc_cm() & CM_MASK) == CM_N64 && !inf_is_64bit() )
      {
        inf_set_app_bitness(64);
        inf_set_max_ea(BADADDR);
      }
    }
    return 0;
  }
};

class idalib_ui_listener_t final : public event_listener_t
{
public:
  std::vector<std::string> captured_lines;
  std::string partial;

  void clear()
  {
    captured_lines.clear();
    partial.clear();
  }

  void append_text(const char *str, size_t len)
  {
    partial.append(str, len);
    size_t pos = 0;
    while ( true )
    {
      size_t nl = partial.find('\n', pos);
      if ( nl == std::string::npos )
      {
        if ( pos > 0 )
          partial.erase(0, pos);
        break;
      }
      std::string line = partial.substr(pos, nl - pos);
      if ( !line.empty() && line.back() == '\r' )
        line.pop_back();
      captured_lines.push_back(std::move(line));
      pos = nl + 1;
    }
  }

  std::string all_text() const
  {
    std::string out;
    for ( const std::string &l : captured_lines )
    {
      out += l;
      out.push_back('\n');
    }
    if ( !partial.empty() )
    {
      out += partial;
      out.push_back('\n');
    }
    return out;
  }

  ssize_t idaapi on_event(ssize_t event, va_list va) override
  {
    if ( event == ui_msg )
    {
      va_list vc;
      va_copy(vc, va);
      const char *fmt = va_arg(vc, const char *);
      va_list fmt_va = va_arg(vc, va_list);
      if ( fmt != nullptr )
      {
        va_list fmt_copy;
        va_copy(fmt_copy, fmt_va);
        qstring formatted;
        formatted.vsprnt(fmt, fmt_copy);
        va_end(fmt_copy);
        append_text(formatted.c_str(), formatted.length());
      }
      va_end(vc);
    }
    else if ( event == ui_mbox )
    {
      va_list vc;
      va_copy(vc, va);
      int kind = va_arg(vc, int);
      if ( kind != mbox_hide && kind != mbox_wait && kind != mbox_replace )
      {
        const char *fmt = va_arg(vc, const char *);
        va_list fmt_va = va_arg(vc, va_list);
        if ( fmt != nullptr )
        {
          va_list fmt_copy;
          va_copy(fmt_copy, fmt_va);
          qstring formatted;
          formatted.vsprnt(fmt, fmt_copy);
          va_end(fmt_copy);
          formatted.append('\n');
          append_text(formatted.c_str(), formatted.length());
        }
      }
      va_end(vc);
    }
    return 0;
  }
};

idalib_idb_listener_t g_idb_listener;
idalib_ui_listener_t g_ui_listener;

bool ensure_idalib_initialized(const fs::path &explicit_idausr, staged_idausr_t *staged)
{
  static bool initialized = false;
  if ( initialized )
    return true;
  force_enable_verify_env();
  if ( !setup_isolated_idausr(explicit_idausr, staged) )
    return false;
  int rc = init_library();
  if ( rc != 0 )
  {
    std::fprintf(stderr, "wasmdec_runner: init_library() failed with rc=%d\n", rc);
    return false;
  }
  enable_console_messages(false);
  hook_event_listener(HT_IDB, &g_idb_listener, nullptr, HKCB_GLOBAL);
  hook_event_listener(HT_UI, &g_ui_listener, nullptr, HKCB_GLOBAL);
  initialized = true;
  return true;
}

// Mode 2: expect <golden-root-or-fixtures...>

struct expect_check_result_t
{
  bool ok = true;
  std::string reason;
};

expect_check_result_t check_expectation(const std::string &line, const std::string &text,
                                        const std::string &status, size_t lines_count)
{
  if ( line.compare(0, 10, "contains: ") == 0 )
  {
    std::string target = trim_ws(line.substr(10));
    if ( text.find(target) != std::string::npos )
      return { true, "" };
    return { false, "missing '" + target + "'" };
  }
  if ( line.compare(0, 14, "not_contains: ") == 0 )
  {
    std::string target = trim_ws(line.substr(14));
    if ( text.find(target) == std::string::npos )
      return { true, "" };
    return { false, "unexpected '" + target + "'" };
  }
  if ( line.compare(0, 7, "regex: ") == 0 )
  {
    std::string pattern = trim_ws(line.substr(7));
    try
    {
      std::regex re(pattern);
      if ( std::regex_search(text, re) )
        return { true, "" };
      return { false, "regex '" + pattern + "' no match" };
    }
    catch ( const std::regex_error &err )
    {
      return { false, std::string("invalid regex '") + pattern + "': " + err.what() };
    }
  }
  if ( line.compare(0, 13, "unsupported: ") == 0 )
  {
    if ( status.compare(0, 6, "INTERR") == 0 )
      return { true, "" };
    return { false, "expected INTERR, got " + status };
  }
  if ( line.compare(0, 8, "status: ") == 0 )
  {
    std::string expected = trim_ws(line.substr(8));
    if ( status == expected )
      return { true, "" };
    return { false, "expected " + expected + ", got " + status };
  }
  if ( line.compare(0, 10, "lines_lt: ") == 0 )
  {
    std::string num_str = trim_ws(line.substr(10));
    long limit = std::strtol(num_str.c_str(), nullptr, 10);
    if ( static_cast<long>(lines_count) < limit )
      return { true, "" };
    return { false, "lines " + std::to_string(lines_count) + " >= " + std::to_string(limit) };
  }
  return { true, "" };
}

// Extract per-function status from _metrics.json
std::map<std::string, std::string> parse_metrics_statuses(const std::string &json)
{
  std::map<std::string, std::string> result;
  size_t key_at = json.find("\"functions\"");
  if ( key_at == std::string::npos )
    return result;
  size_t open = json.find('{', key_at);
  if ( open == std::string::npos )
    return result;
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
    return result;
  std::string block = json.substr(open + 1, close - open - 1);

  auto parse_json_str = [](const std::string &s, size_t i, std::string *out) -> size_t
  {
    while ( i < s.size() && (s[i] == ' ' || s[i] == '\n' || s[i] == '\r' || s[i] == '\t') )
      ++i;
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
  };

  size_t pos = 0;
  while ( pos < block.size() )
  {
    size_t name_start = block.find('"', pos);
    if ( name_start == std::string::npos )
      break;
    std::string name;
    size_t after_name = parse_json_str(block, name_start, &name);
    if ( after_name == std::string::npos )
      break;
    size_t colon = block.find(':', after_name);
    if ( colon == std::string::npos )
      break;
    size_t entry_open = block.find('{', colon);
    if ( entry_open == std::string::npos )
      break;
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
      break;
    std::string entry = block.substr(entry_open + 1, entry_close - entry_open - 1);
    size_t status_key = entry.find("\"status\"");
    if ( status_key != std::string::npos )
    {
      size_t status_colon = entry.find(':', status_key);
      std::string status_val;
      if ( status_colon != std::string::npos
           && parse_json_str(entry, status_colon + 1, &status_val) != std::string::npos )
      {
        result[name] = status_val;
      }
    }
    pos = entry_close + 1;
  }
  return result;
}

bool run_expect_fixture(const fs::path &fixture_dir, std::string *out_status,
                        std::string *out_detail)
{
  std::error_code ec;
  fs::path expect_path = fixture_dir / "expect.txt";
  if ( !fs::exists(expect_path, ec) )
    return false;

  std::string raw_expect = read_file_text(expect_path);
  std::vector<std::string> expectations;
  for ( const std::string &raw_line : split_lines_no_eol(raw_expect) )
  {
    std::string trimmed = trim_ws(raw_line);
    if ( !trimmed.empty() && trimmed[0] != '#' )
      expectations.push_back(trimmed);
  }

  std::vector<fs::path> c_files;
  for ( const auto &entry : fs::directory_iterator(fixture_dir, ec) )
  {
    if ( entry.is_regular_file(ec) && entry.path().extension() == ".c" )
      c_files.push_back(entry.path());
  }
  std::sort(c_files.begin(), c_files.end());

  if ( c_files.empty() )
  {
    fs::path refused = fixture_dir / "_refused.txt";
    if ( fs::exists(refused, ec) )
    {
      std::string ref_text = read_file_text(refused);
      std::vector<std::string> ref_lines = split_lines_no_eol(ref_text);
      std::string last = ref_lines.empty() ? "empty marker" : ref_lines.back().substr(0, 120);
      *out_status = "SKIP_REFUSED";
      *out_detail = last;
      return true;
    }
    *out_status = "FAIL";
    *out_detail = "no .c files";
    return true;
  }

  std::map<std::string, std::string> func_statuses;
  fs::path metrics_path = fixture_dir / "_metrics.json";
  if ( fs::exists(metrics_path, ec) )
    func_statuses = parse_metrics_statuses(read_file_text(metrics_path));

  std::string all_text;
  std::set<std::string> statuses;
  size_t total_lines = 0;
  for ( const fs::path &cf : c_files )
  {
    std::string text = read_file_text(cf);
    all_text += text;
    total_lines += split_lines_no_eol(text).size();
    std::string stem = cf.stem().string();
    auto it = func_statuses.find(stem);
    if ( it != func_statuses.end() )
      statuses.insert(it->second);
    else
      statuses.insert("OK");
  }

  std::vector<std::string> failures;
  for ( const std::string &exp : expectations )
  {
    for ( const std::string &st : statuses )
    {
      expect_check_result_t res = check_expectation(exp, all_text, st, total_lines);
      if ( !res.ok )
      {
        failures.push_back(exp + ": " + res.reason);
        break;
      }
    }
  }

  if ( !failures.empty() )
  {
    std::string joined;
    size_t limit = std::min<size_t>(failures.size(), 5);
    for ( size_t i = 0; i < limit; ++i )
    {
      if ( i > 0 )
        joined += "; ";
      joined += failures[i];
    }
    *out_status = "FAIL";
    *out_detail = joined;
    return true;
  }

  *out_status = "PASS";
  *out_detail = std::to_string(expectations.size()) + " expectations";
  return true;
}

int cmd_expect(int argc, char **argv)
{
  std::error_code ec;
  fs::path repo_root = find_repo_root();
  fs::path default_golden = repo_root / "tests" / "golden";

  std::vector<fs::path> dirs;
  if ( argc == 0 )
  {
    if ( fs::is_directory(default_golden, ec) )
    {
      for ( const auto &entry : fs::directory_iterator(default_golden, ec) )
      {
        if ( entry.is_directory(ec) && fs::exists(entry.path() / "expect.txt", ec) )
          dirs.push_back(entry.path());
      }
      std::sort(dirs.begin(), dirs.end());
    }
  }
  else
  {
    for ( int i = 0; i < argc; ++i )
    {
      fs::path arg_p(argv[i]);
      if ( fs::is_directory(arg_p, ec) && fs::exists(arg_p / "expect.txt", ec) )
      {
        dirs.push_back(arg_p);
      }
      else if ( fs::is_directory(arg_p, ec) )
      {
        // Directory of golden fixtures (e.g. tests/golden)
        std::vector<fs::path> sub;
        for ( const auto &entry : fs::directory_iterator(arg_p, ec) )
        {
          if ( entry.is_directory(ec) && fs::exists(entry.path() / "expect.txt", ec) )
            sub.push_back(entry.path());
        }
        std::sort(sub.begin(), sub.end());
        dirs.insert(dirs.end(), sub.begin(), sub.end());
      }
      else
      {
        dirs.push_back(default_golden / arg_p);
      }
    }
  }

  if ( dirs.empty() )
  {
    std::printf("EXPECT_NO_FIXTURES (no expect.txt found)\n");
    return 0;
  }

  int passed = 0;
  int failed = 0;
  for ( const fs::path &d : dirs )
  {
    std::string status;
    std::string detail;
    if ( !run_expect_fixture(d, &status, &detail) )
      continue;
    std::string name = d.filename().string();
    if ( status == "PASS" )
    {
      std::printf("EXPECT_PASS %s (%s)\n", name.c_str(), detail.c_str());
      ++passed;
    }
    else
    {
      std::printf("EXPECT_FAIL %s: %s\n", name.c_str(), detail.c_str());
      ++failed;
    }
  }
  std::printf("EXPECT_TOTAL pass=%d fail=%d\n", passed, failed);
  return failed > 0 ? 1 : 0;
}

// Mode 1: dump-golden <fixture-dir or wasm> [--out-root <dir>] [--idausr <dir>]

struct dumped_function_t
{
  std::string name;
  std::string safe;
  std::string status;
  long ms = 0;
  long lines = 0;
  long stack = 0;
  bool jumpout = false;
  std::string detail;
  std::string text;
};

struct validator_verdict_info_t
{
  int valid_state = 1; // 1 = true, 0 = false, -1 = null
  std::string reason;
};

validator_verdict_info_t run_wasm_validate(const fs::path &wasm_path)
{
  validator_verdict_info_t info;
  std::string cmd = "wasm-validate '" + wasm_path.string() + "' 2>&1";
  FILE *pipe = popen(cmd.c_str(), "r");
  if ( pipe == nullptr )
  {
    info.valid_state = -1;
    info.reason = "wasm-validate not found";
    return info;
  }
  std::string output;
  char buf[512];
  while ( std::fgets(buf, sizeof(buf), pipe) != nullptr )
    output += buf;
  int status = pclose(pipe);
  int rc = WIFEXITED(status) ? WEXITSTATUS(status) : status;
  if ( rc == 127 )
  {
    info.valid_state = -1;
    info.reason = "wasm-validate not found";
    return info;
  }
  if ( rc == 0 )
  {
    info.valid_state = 1;
    info.reason = "";
    return info;
  }
  info.valid_state = 0;
  std::string trimmed = trim_ws(output);
  if ( trimmed.size() > 300 )
    trimmed.resize(300);
  info.reason = trimmed.empty() ? "wasm-validate rejected" : trimmed;
  return info;
}

std::string format_validator_json(const validator_verdict_info_t &v)
{
  std::ostringstream oss;
  oss << "{\n";
  if ( v.valid_state < 0 )
    oss << " \"valid\": null,\n";
  else if ( v.valid_state > 0 )
    oss << " \"valid\": true,\n";
  else
    oss << " \"valid\": false,\n";
  oss << " \"reason\": \"" << json_escape(v.reason) << "\"\n";
  oss << "}";
  return oss.str();
}

std::string format_metrics_json(const std::string &fixture_filename, long wall_ms, bool timed_out,
                                const std::vector<dumped_function_t> &funcs, long unsupported_msgs)
{
  std::ostringstream oss;
  oss << "{\n";
  oss << " \"fixture\": \"" << json_escape(fixture_filename) << "\",\n";
  oss << " \"wall_ms\": " << wall_ms << ",\n";
  oss << " \"timeout\": " << (timed_out ? "true" : "false") << ",\n";
  if ( funcs.empty() )
  {
    oss << " \"functions\": {},\n";
  }
  else
  {
    oss << " \"functions\": {\n";
    for ( size_t i = 0; i < funcs.size(); ++i )
    {
      const dumped_function_t &fn = funcs[i];
      oss << "  \"" << json_escape(fn.name) << "\": {\n";
      oss << "   \"status\": \"" << json_escape(fn.status) << "\",\n";
      oss << "   \"ms\": " << fn.ms << ",\n";
      oss << "   \"lines\": " << fn.lines << ",\n";
      oss << "   \"stack\": " << fn.stack << ",\n";
      oss << "   \"jumpout\": " << (fn.jumpout ? "true" : "false") << ",\n";
      oss << "   \"detail\": \"" << json_escape(fn.detail) << "\"\n";
      oss << "  }" << (i + 1 < funcs.size() ? "," : "") << "\n";
    }
    oss << " },\n";
  }

  long ok_cnt = 0;
  long interr_cnt = 0;
  long exc_cnt = 0;
  long verify_cnt = 0;
  long jumpout_cnt = 0;
  for ( const dumped_function_t &fn : funcs )
  {
    if ( fn.status == "OK" )
      ++ok_cnt;
    else if ( fn.status == "INTERR" )
      ++interr_cnt;
    else if ( fn.status == "EXC" )
      ++exc_cnt;
    else if ( fn.status == "VERIFY" )
      ++verify_cnt;
    if ( fn.jumpout )
      ++jumpout_cnt;
  }

  oss << " \"totals\": {\n";
  oss << "  \"functions\": " << funcs.size() << ",\n";
  oss << "  \"ok\": " << ok_cnt << ",\n";
  oss << "  \"interr\": " << interr_cnt << ",\n";
  oss << "  \"exc\": " << exc_cnt << ",\n";
  oss << "  \"verify\": " << verify_cnt << ",\n";
  oss << "  \"jumpout\": " << jumpout_cnt << ",\n";
  oss << "  \"unsupported_msgs\": " << unsupported_msgs << "\n";
  oss << " }\n";
  oss << "}";
  return oss.str();
}

bool prepare_scratch_wasm(const fs::path &src_wasm, fs::path *out_work_dir, fs::path *out_wasm)
{
  char tmpl[] = "/private/tmp/wasmdec_runner_work_XXXXXX";
  char *made = mkdtemp(tmpl);
  if ( made == nullptr )
    return false;
  fs::path work(made);
  std::error_code ec;
  fs::path dst = work / src_wasm.filename();
  if ( !fs::copy_file(src_wasm, dst, fs::copy_options::overwrite_existing, ec) )
    return false;
  // Copy sidecar .debug.wasm if present (for external_debug_info fixtures such as dwarf_ext.wasm)
  fs::path sidecar = src_wasm.parent_path() / (src_wasm.stem().string() + ".debug.wasm");
  if ( fs::exists(sidecar, ec) )
  {
    fs::copy_file(sidecar, work / sidecar.filename(), fs::copy_options::overwrite_existing, ec);
  }
  *out_work_dir = work;
  *out_wasm = dst;
  return true;
}

bool dump_one_fixture(const fs::path &wasm_path, const fs::path &out_dir, long *out_wall_ms,
                      size_t *out_ok, size_t *out_total, size_t *out_interr, size_t *out_jumpout)
{
  std::error_code ec;
  fs::create_directories(out_dir, ec);

  fs::path work_dir;
  fs::path local_wasm;
  if ( !prepare_scratch_wasm(wasm_path, &work_dir, &local_wasm) )
  {
    std::fprintf(stderr, "wasmdec_runner: failed to stage scratch wasm for %s\n",
                 wasm_path.string().c_str());
    return false;
  }

  auto t_start = std::chrono::steady_clock::now();
  g_ui_listener.clear();

  int open_rc = open_database(local_wasm.c_str(), true);
  std::vector<dumped_function_t> funcs;
  long unsup_msgs = 0;

  if ( open_rc == 0 )
  {
    auto_wait();
    if ( get_hexdsp() == nullptr )
      init_hexrays_plugin();
    change_hexrays_config("ARG_HINTS_MODE = 0");

    const int decomp_flags = has_debug_token("phases") ? DECOMP_NO_CACHE : 0;
    const size_t total = get_func_qty();
    std::map<std::string, int> used_names;

    for ( size_t i = 0; i < total; ++i )
    {
      func_entry_info_t fi;
      if ( !get_func_entry_info_by_num(&fi, i) )
        continue;
      ea_t ea = fi.start_ea;
      qstring qname;
      get_func_name(&qname, ea);
      std::string name = qname.empty() ? [&]()
      {
        char buf[32];
        qsnprintf(buf, sizeof(buf), "sub_%llX", static_cast<unsigned long long>(ea));
        return std::string(buf);
      }()
                                       : std::string(qname.c_str());

      std::string base_safe = safe_func_name(name);
      std::string safe = base_safe;
      auto it_used = used_names.find(base_safe);
      if ( it_used != used_names.end() )
      {
        char prefix[16];
        qsnprintf(prefix, sizeof(prefix), "%03d_", it_used->second);
        safe = std::string(prefix) + base_safe;
        it_used->second += 1;
      }
      else
      {
        used_names[base_safe] = 1;
      }

      auto fn_start = std::chrono::steady_clock::now();
      dumped_function_t df;
      df.name = name;
      df.safe = safe;
      df.status = "OK";

      hexrays_failure_t hf;
      cfuncptr_t cfunc(nullptr);
      try
      {
        cfunc = decompile_function(ea, &hf, decomp_flags);
      }
      catch ( const vd_failure_t &vf )
      {
        cfunc = cfuncptr_t(nullptr);
        df.status = "EXC";
        df.detail = std::string("vd_failure_t:") + vf.hf.str.c_str();
        if ( df.detail.size() > 120 )
          df.detail.resize(120);
      }
      catch ( const std::exception &ex )
      {
        cfunc = cfuncptr_t(nullptr);
        df.status = "EXC";
        df.detail = std::string("Exception:") + ex.what();
        if ( df.detail.size() > 120 )
          df.detail.resize(120);
      }
      catch ( ... )
      {
        cfunc = cfuncptr_t(nullptr);
        df.status = "EXC";
        df.detail = "unknown exception";
      }

      if ( cfunc == nullptr && df.status == "OK" )
      {
        df.status = "INTERR";
        char det[64];
        qsnprintf(det, sizeof(det), "code=%d errea=0x%llx", hf.code,
                  static_cast<unsigned long long>(hf.errea));
        df.detail = det;
      }
      else if ( cfunc != nullptr )
      {
        try
        {
          // Always verify both cfunc and mba unconditionally
          cfunc->verify(ALLOW_UNUSED_LABELS, true);
          cfunc->mba->verify(true);
          const strvec_t &sv = cfunc->get_pseudocode();
          std::string text;
          for ( size_t k = 0; k < sv.size(); ++k )
          {
            qstring clean;
            tag_remove(&clean, sv[k].line);
            if ( k > 0 )
              text.push_back('\n');
            text.append(clean.c_str(), clean.length());
          }
          df.text = std::move(text);
        }
        catch ( const std::exception &ex )
        {
          df.status = "VERIFY";
          df.text.clear();
          df.detail = ex.what();
          if ( df.detail.size() > 120 )
            df.detail.resize(120);
        }
        catch ( ... )
        {
          df.status = "VERIFY";
          df.text.clear();
          df.detail = "verify failed";
        }
      }

      auto fn_end = std::chrono::steady_clock::now();
      df.ms = std::chrono::duration_cast<std::chrono::milliseconds>(fn_end - fn_start).count();
      if ( !df.text.empty() )
      {
        df.lines = static_cast<long>(split_lines_no_eol(df.text).size());
        df.stack = static_cast<long>(count_substr(df.text, "STACK["));
        df.jumpout = df.text.find("JUMPOUT(") != std::string::npos;
      }
      funcs.push_back(std::move(df));
    }
  }

  // Collect IDA output messages captured via HT_UI (ui_msg)
  bool module_rejected = false;
  std::string module_rejected_reason;
  // Debug: WASMHEX_DEBUG=goloader echoes the loader's msg() lines
  // (step-C counters, per-function skip reasons) to stderr — they are
  // captured, but nothing else prints them under idalib.
  qstring goloader_env;
  const bool echo_loader_msgs
    = qgetenv("WASMHEX_DEBUG", &goloader_env) && goloader_env.find("goloader") != qstring::npos;
  for ( const std::string &line : g_ui_listener.captured_lines )
  {
    if ( echo_loader_msgs && line.find("wasmhex:") != std::string::npos )
      std::fprintf(stderr, "WASMHEX_LOADER_MSG %s\n", line.c_str());
    if ( line.find("WASMHEX_F5_UNSUPPORTED") != std::string::npos
         || line.find("WASMHEX_DYNAMIC_CALL_UNSUPPORTED") != std::string::npos )
    {
      ++unsup_msgs;
    }
    if ( line.find("wasmhex: module rejected") != std::string::npos )
    {
      module_rejected = true;
      if ( module_rejected_reason.empty() )
        module_rejected_reason = trim_ws(line);
    }
  }

  if ( open_rc == 0 )
  {
    set_database_flag(DBFL_KILL);
    close_database(false);
  }
  fs::remove_all(work_dir, ec);

  auto t_end = std::chrono::steady_clock::now();
  long wall_ms = std::chrono::duration_cast<std::chrono::milliseconds>(t_end - t_start).count();

  // Write .c files for functions with text
  for ( const dumped_function_t &fn : funcs )
  {
    if ( !fn.text.empty() )
    {
      write_file_text(out_dir / (fn.safe + ".c"), fn.text + "\n");
    }
  }

  // Write _metrics.json
  std::string metrics_str
    = format_metrics_json(wasm_path.filename().string(), wall_ms, false, funcs, unsup_msgs);
  write_file_text(out_dir / "_metrics.json", metrics_str);

  // Write _validator.json
  validator_verdict_info_t vinfo = run_wasm_validate(wasm_path);
  write_file_text(out_dir / "_validator.json", format_validator_json(vinfo));

  // Refused-marker contract (_refused.txt)
  std::map<std::string, const dumped_function_t *> refused;
  for ( const dumped_function_t &fn : funcs )
  {
    if ( fn.status != "OK" )
      refused[fn.name] = &fn;
  }

  if ( !refused.empty() || module_rejected )
  {
    std::ostringstream ref_oss;
    ref_oss << "# T0-era refused-marker: functions below currently FAIL F5;"
               " their .c files are intentionally absent (no stale copies).\n";
    ref_oss << "# dumped: " << iso_timestamp_seconds() << "\n";
    if ( module_rejected )
    {
      ref_oss << "MODULE\tREJECTED\t" << module_rejected_reason << "\n";
      for ( const auto &entry : fs::directory_iterator(out_dir, ec) )
      {
        if ( entry.is_regular_file(ec) && entry.path().extension() == ".c" )
          fs::remove(entry.path(), ec);
      }
    }
    for ( const auto &kv : refused )
    {
      const dumped_function_t *fn = kv.second;
      ref_oss << fn->name << "\t" << fn->status << "\t" << fn->detail << "\n";
    }
    write_file_text(out_dir / "_refused.txt", ref_oss.str());
    for ( const auto &kv : refused )
    {
      std::string safe = safe_func_name(kv.first);
      fs::remove(out_dir / (safe + ".c"), ec);
    }
  }
  else
  {
    fs::remove(out_dir / "_refused.txt", ec);
  }

  size_t ok_cnt = 0;
  size_t interr_cnt = 0;
  size_t jumpout_cnt = 0;
  for ( const dumped_function_t &fn : funcs )
  {
    if ( fn.status == "OK" )
      ++ok_cnt;
    if ( fn.status == "INTERR" )
      ++interr_cnt;
    if ( fn.jumpout )
      ++jumpout_cnt;
  }
  *out_wall_ms = wall_ms;
  *out_ok = ok_cnt;
  *out_total = funcs.size();
  *out_interr = interr_cnt;
  *out_jumpout = jumpout_cnt;
  return true;
}

std::vector<fs::path> discover_fixtures_in_dir(const fs::path &dir)
{
  std::error_code ec;
  std::vector<fs::path> modules;
  auto collect_sorted = [&](const fs::path &d)
  {
    if ( !fs::is_directory(d, ec) )
      return;
    std::vector<fs::path> sub;
    for ( const auto &entry : fs::directory_iterator(d, ec) )
    {
      if ( !entry.is_regular_file(ec) || entry.path().extension() != ".wasm" )
        continue;
      std::string fname = entry.path().filename().string();
      // or fixture discovery intended when targeting primary fixtures
      sub.push_back(entry.path());
    }
    std::sort(sub.begin(), sub.end(),
              [&](const fs::path &a, const fs::path &b)
              {
                auto sa = fs::file_size(a, ec);
                auto sb = fs::file_size(b, ec);
                if ( sa != sb )
                  return sa < sb;
                return a.filename().string() < b.filename().string();
              });
    modules.insert(modules.end(), sub.begin(), sub.end());
  };
  collect_sorted(dir);
  if ( fs::is_directory(dir / "brtable", ec) )
    collect_sorted(dir / "brtable");
  return modules;
}

int cmd_dump_golden(int argc, char **argv)
{
  fs::path repo_root = find_repo_root();
  fs::path out_root = repo_root / "tests" / "golden";
  fs::path explicit_idausr;
  std::vector<std::string> targets;

  for ( int i = 0; i < argc; ++i )
  {
    if ( std::strcmp(argv[i], "--out-root") == 0 && i + 1 < argc )
      out_root = argv[++i];
    else if ( std::strcmp(argv[i], "--idausr") == 0 && i + 1 < argc )
      explicit_idausr = argv[++i];
    else if ( std::strcmp(argv[i], "--fixture") == 0 && i + 1 < argc )
      targets.push_back(argv[++i]);
    else
      targets.push_back(argv[i]);
  }

  std::error_code ec;
  std::vector<fs::path> modules;
  if ( targets.empty() )
  {
    modules = discover_fixtures_in_dir(repo_root / "tests" / "fixtures");
  }
  else
  {
    for ( const std::string &t : targets )
    {
      fs::path p(t);
      if ( fs::is_directory(p, ec) )
      {
        auto sub = discover_fixtures_in_dir(p);
        modules.insert(modules.end(), sub.begin(), sub.end());
      }
      else if ( fs::is_regular_file(p, ec) )
      {
        modules.push_back(fs::canonical(p, ec));
      }
      else
      {
        // Try resolving as fixture name under tests/fixtures or tests/fixtures/brtable
        fs::path cand1 = repo_root / "tests" / "fixtures" / (t + ".wasm");
        fs::path cand2 = repo_root / "tests" / "fixtures" / "brtable" / (t + ".wasm");
        fs::path cand3 = repo_root / "tests" / "fixtures" / t;
        if ( fs::is_regular_file(cand1, ec) )
          modules.push_back(fs::canonical(cand1, ec));
        else if ( fs::is_regular_file(cand2, ec) )
          modules.push_back(fs::canonical(cand2, ec));
        else if ( fs::is_regular_file(cand3, ec) )
          modules.push_back(fs::canonical(cand3, ec));
        else
        {
          std::fprintf(stderr, "wasmdec_runner: fixture not found: %s\n", t.c_str());
          return 2;
        }
      }
    }
  }

  staged_idausr_t staged;
  if ( !ensure_idalib_initialized(explicit_idausr, &staged) )
    return 2;

  std::printf("ORACLE_PLAN fixtures=%zu\n", modules.size());
  std::fflush(stdout);

  auto grand_start = std::chrono::steady_clock::now();
  std::vector<std::string> failures;

  for ( size_t idx = 0; idx < modules.size(); ++idx )
  {
    const fs::path &mod = modules[idx];
    fs::path out_dir = out_root / mod.stem();
    long wall_ms = 0;
    size_t ok_cnt = 0;
    size_t total_cnt = 0;
    size_t interr_cnt = 0;
    size_t jumpout_cnt = 0;
    if ( !dump_one_fixture(mod, out_dir, &wall_ms, &ok_cnt, &total_cnt, &interr_cnt, &jumpout_cnt) )
    {
      failures.push_back(mod.filename().string());
      continue;
    }
    std::printf("ORACLE_FIXTURE %zu/%zu %s ok=%zu/%zu interr=%zu jumpout=%zu %ldms\n", idx + 1,
                modules.size(), mod.filename().string().c_str(), ok_cnt, total_cnt, interr_cnt,
                jumpout_cnt, wall_ms);
    std::fflush(stdout);
    if ( interr_cnt > 0 )
      failures.push_back(mod.filename().string());
  }

  auto grand_end = std::chrono::steady_clock::now();
  long elapsed_s
    = std::chrono::duration_cast<std::chrono::seconds>(grand_end - grand_start).count();
  std::printf("ORACLE_TOTAL fixtures=%zu seconds=%ld with_issues=%zu\n", modules.size(), elapsed_s,
              failures.size());
  if ( !failures.empty() )
  {
    std::printf("ORACLE_ISSUES ");
    size_t limit = std::min<size_t>(failures.size(), 20);
    for ( size_t i = 0; i < limit; ++i )
    {
      if ( i > 0 )
        std::printf(", ");
      std::printf("%s", failures[i].c_str());
    }
    std::printf("\n");
  }
  std::fflush(stdout);
  return 0;
}

// Mode 3: f5 <wasm> [--stride N] [--shard S] [--limit L] [--start-ea EA]
// Mode 3: f5 <wasm> [--stride N] [--shard S] [--limit L] [--start-ea EA]
//                   [--stop-ea EA] [--start-index I] [--stop-index I]
//                   [--bounds hidden|explicit] [--idausr <dir>]

bool parse_opt_int64(const char *str, long long *out)
{
  if ( str == nullptr || *str == '\0' )
    return false;
  char *end = nullptr;
  long long v = std::strtoll(str, &end, 0);
  if ( end == str || *end != '\0' )
    return false;
  *out = v;
  return true;
}

int cmd_f5(int argc, char **argv)
{
  fs::path wasm_path;
  fs::path explicit_idausr;
  long long stride = 1;
  long long shard = 0;
  bool have_limit = false;
  long long limit = 0;
  bool have_start_ea = false;
  ea_t start_ea = 0;
  bool have_stop_ea = false;
  ea_t stop_ea = 0;
  bool have_start_index = false;
  long long start_index = 0;
  bool have_stop_index = false;
  long long stop_index = 0;

  long long env_val = 0;
  if ( parse_opt_int64(read_env_str("WASMHEX_STRIDE").c_str(), &env_val) )
    stride = env_val;
  if ( parse_opt_int64(read_env_str("WASMHEX_SHARD").c_str(), &env_val) )
    shard = env_val;
  if ( parse_opt_int64(read_env_str("WASMHEX_LIMIT").c_str(), &env_val) )
  {
    have_limit = true;
    limit = env_val;
  }
  if ( parse_opt_int64(read_env_str("WASMHEX_START_EA").c_str(), &env_val) )
  {
    have_start_ea = true;
    start_ea = static_cast<ea_t>(env_val);
  }
  if ( parse_opt_int64(read_env_str("WASMHEX_STOP_EA").c_str(), &env_val) )
  {
    have_stop_ea = true;
    stop_ea = static_cast<ea_t>(env_val);
  }
  if ( parse_opt_int64(read_env_str("WASMHEX_START_INDEX").c_str(), &env_val) )
  {
    have_start_index = true;
    start_index = env_val;
  }
  if ( parse_opt_int64(read_env_str("WASMHEX_STOP_INDEX").c_str(), &env_val) )
  {
    have_stop_index = true;
    stop_index = env_val;
  }

  for ( int i = 0; i < argc; ++i )
  {
    if ( std::strcmp(argv[i], "--stride") == 0 && i + 1 < argc )
      stride = std::strtoll(argv[++i], nullptr, 0);
    else if ( std::strcmp(argv[i], "--shard") == 0 && i + 1 < argc )
      shard = std::strtoll(argv[++i], nullptr, 0);
    else if ( std::strcmp(argv[i], "--limit") == 0 && i + 1 < argc )
    {
      have_limit = true;
      limit = std::strtoll(argv[++i], nullptr, 0);
    }
    else if ( std::strcmp(argv[i], "--start-ea") == 0 && i + 1 < argc )
    {
      have_start_ea = true;
      start_ea = static_cast<ea_t>(std::strtoull(argv[++i], nullptr, 0));
    }
    else if ( std::strcmp(argv[i], "--stop-ea") == 0 && i + 1 < argc )
    {
      have_stop_ea = true;
      stop_ea = static_cast<ea_t>(std::strtoull(argv[++i], nullptr, 0));
    }
    else if ( std::strcmp(argv[i], "--start-index") == 0 && i + 1 < argc )
    {
      have_start_index = true;
      start_index = std::strtoll(argv[++i], nullptr, 0);
    }
    else if ( std::strcmp(argv[i], "--stop-index") == 0 && i + 1 < argc )
    {
      have_stop_index = true;
      stop_index = std::strtoll(argv[++i], nullptr, 0);
    }
    else if ( std::strcmp(argv[i], "--bounds") == 0 && i + 1 < argc )
    {
      qsetenv("WASMHEX_BOUNDS", argv[++i]);
    }
    else if ( std::strcmp(argv[i], "--idausr") == 0 && i + 1 < argc )
    {
      explicit_idausr = argv[++i];
    }
    else if ( wasm_path.empty() )
    {
      wasm_path = argv[i];
    }
    else
    {
      std::fprintf(stderr, "wasmdec_runner f5: unexpected argument '%s'\n", argv[i]);
      return 2;
    }
  }

  if ( wasm_path.empty() )
  {
    std::fprintf(stderr,
                 "usage: wasmdec_runner f5 <module.wasm> [--stride N] [--shard S] [--limit L]\n");
    return 2;
  }
  if ( stride < 1 )
  {
    std::fprintf(stderr, "WASMHEX_STRIDE must be positive\n");
    return 2;
  }
  if ( stride > 1 && (shard < 0 || shard >= stride) )
  {
    std::fprintf(stderr, "WASMHEX_SHARD requires 0 <= shard < WASMHEX_STRIDE\n");
    return 2;
  }

  std::error_code ec;
  if ( !fs::is_regular_file(wasm_path, ec) )
  {
    std::fprintf(stderr, "wasmdec_runner f5: file not found: %s\n", wasm_path.string().c_str());
    return 2;
  }

  staged_idausr_t staged;
  if ( !ensure_idalib_initialized(explicit_idausr, &staged) )
    return 2;

  fs::path work_dir;
  fs::path local_wasm;
  if ( !prepare_scratch_wasm(wasm_path, &work_dir, &local_wasm) )
  {
    std::fprintf(stderr, "wasmdec_runner f5: failed to stage scratch wasm\n");
    return 2;
  }

  auto t_open_start = std::chrono::steady_clock::now();
  int open_rc = open_database(local_wasm.c_str(), true);
  if ( open_rc != 0 )
  {
    std::fprintf(stderr, "wasmdec_runner f5: open_database failed with rc=%d\n", open_rc);
    fs::remove_all(work_dir, ec);
    return 1;
  }
  auto_wait();
  if ( get_hexdsp() == nullptr )
    init_hexrays_plugin();
  auto t_open_end = std::chrono::steady_clock::now();

  const size_t total_db_funcs = get_func_qty();
  std::vector<ea_t> functions;
  functions.reserve(total_db_funcs);
  for ( size_t i = 0; i < total_db_funcs; ++i )
  {
    func_entry_info_t fi;
    if ( get_func_entry_info_by_num(&fi, i) )
      functions.push_back(fi.start_ea);
  }

  if ( have_start_index )
  {
    size_t idx = start_index > 0 ? static_cast<size_t>(start_index) : 0;
    if ( idx < functions.size() )
      functions.erase(functions.begin(), functions.begin() + static_cast<std::ptrdiff_t>(idx));
    else
      functions.clear();
  }
  if ( have_stop_index )
  {
    long long rel_stop = stop_index - (have_start_index ? start_index : 0);
    size_t keep = rel_stop > 0 ? static_cast<size_t>(rel_stop) : 0;
    if ( keep < functions.size() )
      functions.resize(keep);
  }
  if ( have_start_ea )
  {
    std::vector<ea_t> filtered;
    for ( ea_t ea : functions )
      if ( ea >= start_ea )
        filtered.push_back(ea);
    functions.swap(filtered);
  }
  if ( have_stop_ea )
  {
    std::vector<ea_t> filtered;
    for ( ea_t ea : functions )
      if ( ea <= stop_ea )
        filtered.push_back(ea);
    functions.swap(filtered);
  }
  if ( stride > 1 )
  {
    std::vector<ea_t> strided;
    for ( size_t i = static_cast<size_t>(shard); i < functions.size();
          i += static_cast<size_t>(stride) )
    {
      strided.push_back(functions[i]);
    }
    functions.swap(strided);
  }
  if ( have_limit && limit >= 0 && static_cast<size_t>(limit) < functions.size() )
  {
    functions.resize(static_cast<size_t>(limit));
  }

  const int decomp_flags = has_debug_token("phases") ? DECOMP_NO_CACHE : 0;
  size_t failures_count = 0;
  size_t refused_count = 0;
  std::map<std::string, size_t> categories;
  std::vector<std::string> category_order;
  std::string f5_subset_stream;

  auto emit_f5_line = [&](const std::string &line)
  {
    std::printf("%s\n", line.c_str());
    std::fflush(stdout);
    f5_subset_stream += line;
    f5_subset_stream.push_back('\n');
  };

  auto record_failure
    = [&](ea_t ea, const std::string &name, const std::string &raw_msg, bool refused = false)
  {
    std::string trimmed = trim_ws(raw_msg);
    if ( trimmed.empty() )
      trimmed = "RuntimeError";
    std::vector<std::string> msg_lines = split_lines_no_eol(trimmed);
    std::string category = msg_lines.empty() ? "RuntimeError" : msg_lines[0];
    if ( categories.find(category) == categories.end() )
      category_order.push_back(category);
    categories[category] += 1;
    // MERR_BADARCH (-31) is the sanctioned fail-closed
    // refusal (unsupported shape / deliberate degrade, always with a msg()
    // diagnostic) — report it in the FAIL/CATEGORY lines but do not fail
    // the run. Engine failures (INTERR, exceptions, verify) still do.
    if ( refused )
      ++refused_count;
    else
      ++failures_count;
    if ( failures_count + refused_count <= 100 )
    {
      char buf[512];
      qsnprintf(buf, sizeof(buf), "WASMHEX_F5_FAIL 0x%llx %s: %s",
                static_cast<unsigned long long>(ea), name.c_str(), category.c_str());
      emit_f5_line(buf);
    }
  };

  auto t_decomp_start = std::chrono::steady_clock::now();

  for ( size_t idx = 0; idx < functions.size(); ++idx )
  {
    const size_t count = idx + 1;
    const ea_t ea = functions[idx];
    qstring qname;
    get_func_name(&qname, ea);
    std::string name(qname.c_str());

    hexrays_failure_t hf;
    try
    {
      cfuncptr_t cfunc = decompile_function(ea, &hf, decomp_flags);
      if ( cfunc == nullptr )
      {
        qstring desc = hf.desc();
        char errbuf[512];
        qsnprintf(errbuf, sizeof(errbuf), "%d 0x%llx %s", hf.code,
                  static_cast<unsigned long long>(hf.errea), desc.c_str());
        record_failure(ea, name, errbuf, hf.code == MERR_BADARCH);
      }
      else
      {
        // Unconditional verify on every decompiled function
        cfunc->verify(ALLOW_UNUSED_LABELS, true);
        cfunc->mba->verify(true);
        const strvec_t &sv = cfunc->get_pseudocode();
        std::string pseudocode;
        for ( size_t k = 0; k < sv.size(); ++k )
        {
          qstring clean;
          tag_remove(&clean, sv[k].line);
          if ( k > 0 )
            pseudocode.push_back('\n');
          pseudocode.append(clean.c_str(), clean.length());
        }
        if ( pseudocode.find("JUMPOUT(") != std::string::npos )
          record_failure(ea, name, "pseudocode contains JUMPOUT");
        else if ( pseudocode.find("STACK[") != std::string::npos )
          record_failure(ea, name, "pseudocode contains STACK[]");
      }
    }
    catch ( const vd_failure_t &vf )
    {
      qstring desc = vf.hf.desc();
      record_failure(ea, name, desc.c_str());
    }
    catch ( const std::exception &ex )
    {
      record_failure(ea, name, ex.what());
    }
    catch ( ... )
    {
      record_failure(ea, name, "unknown exception");
    }

    if ( count % 100 == 0 )
    {
      char pbuf[128];
      qsnprintf(pbuf, sizeof(pbuf), "WASMHEX_F5_PROGRESS %zu/%zu failures=%zu", count,
                functions.size(), failures_count);
      emit_f5_line(pbuf);
    }
  }

  auto t_decomp_end = std::chrono::steady_clock::now();

  // Sort categories by descending count (preserving insertion order on ties)
  std::stable_sort(category_order.begin(), category_order.end(),
                   [&](const std::string &a, const std::string &b)
                   { return categories[a] > categories[b]; });
  for ( const std::string &cat : category_order )
  {
    char cbuf[512];
    qsnprintf(cbuf, sizeof(cbuf), "WASMHEX_F5_CATEGORY %zu %s", categories[cat], cat.c_str());
    emit_f5_line(cbuf);
  }

  {
    char dbuf[128];
    qsnprintf(dbuf, sizeof(dbuf), "WASMHEX_F5_DONE total=%zu failures=%zu", functions.size(),
              failures_count);
    emit_f5_line(dbuf);
  }

  // Compute F5-subset sha256 (exact equivalent of `grep '^WASMHEX_F5' | shasum -a 256`)
  std::string f5_sha = sha256_hex(f5_subset_stream);
  std::printf("WASMHEX_F5_REFUSED count=%zu\n", refused_count);
  long open_ms
    = std::chrono::duration_cast<std::chrono::milliseconds>(t_open_end - t_open_start).count();
  long decomp_ms
    = std::chrono::duration_cast<std::chrono::milliseconds>(t_decomp_end - t_decomp_start).count();
  std::printf("F5_SUBSET_SHA256 %s\n", f5_sha.c_str());
  std::printf("F5_TIMING open_ms=%ld decompile_ms=%ld total_ms=%ld\n", open_ms, decomp_ms,
              open_ms + decomp_ms);
  std::fflush(stdout);

  set_database_flag(DBFL_KILL);
  close_database(false);
  fs::remove_all(work_dir, ec);

  return failures_count > 0 ? 1 : 0;
}

#include "wasmhex_checks.inc"

int cmd_abi_probe(int argc, char **argv)
{
  fs::path wasm_path;
  fs::path explicit_idausr;
  std::string want_function;
  for ( int i = 0; i < argc; ++i )
  {
    if ( std::strcmp(argv[i], "--idausr") == 0 && i + 1 < argc )
      explicit_idausr = argv[++i];
    else if ( std::strcmp(argv[i], "--function") == 0 && i + 1 < argc )
      want_function = argv[++i];
    else if ( wasm_path.empty() )
      wasm_path = argv[i];
    else
    {
      std::fprintf(stderr, "wasmdec_runner abi-probe: unexpected argument '%s'\n", argv[i]);
      return 2;
    }
  }
  if ( wasm_path.empty() )
  {
    std::fprintf(
      stderr,
      "usage: wasmdec_runner abi-probe <module.wasm> [--function <name>] [--idausr <dir>]\n");
    return 2;
  }
  std::error_code ec;
  if ( !fs::is_regular_file(wasm_path, ec) )
  {
    std::fprintf(stderr, "wasmdec_runner abi-probe: file not found: %s\n",
                 wasm_path.string().c_str());
    return 2;
  }

  staged_idausr_t staged;
  if ( !ensure_idalib_initialized(explicit_idausr, &staged) )
    return 2;

  scratch_db_session_t sess;
  if ( !sess.open(wasm_path) )
  {
    std::fprintf(stderr, "wasmdec_runner abi-probe: open_database failed rc=%d\n", sess.open_rc);
    return 1;
  }

  // Section 1: machine-register microcode map (reg2mreg / mreg2reg)
  // For every processor register, print the mreg the engine assigned and the
  // widths the engine believes that mreg can address. mreg2reg(m, w) == reg
  // means the engine's regmap has an entry binding this mreg at width w.
  std::printf("WASMHEX_ABI_PROBE regs_num=%d mr_first=%d\n", int(PH.regs_num), int(mr_first));
  int machine_regs_with_8byte = 0;
  for ( int i = 0; i < PH.regs_num; ++i )
  {
    const mreg_t m = reg2mreg(i);
    if ( m == mr_none )
    {
      std::printf("WASMHEX_ABI_PROBE reg %d name=%s reg2mreg=NONE\n", i,
                  PH.reg_names[i] != nullptr ? PH.reg_names[i] : "?");
      continue;
    }
    const int r1 = mreg2reg(m, 1);
    const int r2 = mreg2reg(m, 2);
    const int r4 = mreg2reg(m, 4);
    const int r8 = mreg2reg(m, 8);
    if ( r8 == i )
      ++machine_regs_with_8byte;
    std::printf(
      "WASMHEX_ABI_PROBE reg %d name=%s mreg=%d is_kreg=%d mreg2reg(w1)=%d (w2)=%d (w4)=%d (w8)=%d\n",
      i, PH.reg_names[i] != nullptr ? PH.reg_names[i] : "?", int(m), is_kreg(m) ? 1 : 0, r1, r2, r4,
      r8);
  }
  std::printf("WASMHEX_ABI_PROBE regs_with_8byte_mreg_entry=%d\n", machine_regs_with_8byte);

  // Section 2: argloc acceptance (verify_argloc)
  // 8-byte value in a single register location vs the stock reg2 pair.
  for ( int reg = 0; reg < 10 && reg < PH.regs_num; ++reg )
  {
    argloc_t single;
    single.set_reg1(reg);
    const int rc_single8 = verify_argloc(single, 8, nullptr);
    const int rc_single4 = verify_argloc(single, 4, nullptr);
    argloc_t pair;
    pair.set_reg2(reg, (reg + 1) % PH.regs_num);
    const int rc_pair8 = verify_argloc(pair, 8, nullptr);
    std::printf(
      "WASMHEX_ABI_PROBE argloc reg=%d reg1_size8_rc=%d reg1_size4_rc=%d reg2_size8_rc=%d\n", reg,
      rc_single8, rc_single4, rc_pair8);
  }

  // Section 3: end-to-end forced 8-byte single-reg return
  // Apply a usercall type with retloc = reg1(r0) and an 8-byte return to a
  // real function, decompile it, and record what the engine does. The stock
  // ABI shape (reg2(r0, r1)) is the control.
  ea_t probe_ea = BADADDR;
  std::string probe_name;
  {
    const size_t qty = get_func_qty();
    for ( size_t i = 0; i < qty; ++i )
    {
      func_entry_info_t fi;
      if ( !get_func_entry_info_by_num(&fi, i) )
        continue;
      qstring qname;
      get_func_name(&qname, fi.start_ea);
      std::string nm(qname.c_str());
      if ( nm.find("i64") != std::string::npos )
      {
        probe_ea = fi.start_ea;
        probe_name = nm;
        break;
      }
    }
    if ( probe_ea == BADADDR && qty > 0 )
    {
      func_entry_info_t fi;
      if ( get_func_entry_info_by_num(&fi, 0) )
      {
        probe_ea = fi.start_ea;
        qstring qname;
        get_func_name(&qname, probe_ea);
        probe_name = qname.c_str();
      }
    }
  }
  if ( probe_ea == BADADDR )
  {
    std::printf("WASMHEX_ABI_PROBE e2e skipped (no function in module)\n");
  }
  else
  {
    // Named-function census mode: decompile the requested function and count
    // the pair-display debt markers in its pseudocode (no type forcing).
    if ( !want_function.empty() )
    {
      ea_t named_ea = BADADDR;
      const size_t qty = get_func_qty();
      for ( size_t i = 0; i < qty; ++i )
      {
        func_entry_info_t fi;
        if ( !get_func_entry_info_by_num(&fi, i) )
          continue;
        qstring qname;
        get_func_name(&qname, fi.start_ea);
        if ( std::string(qname.c_str()).find(want_function) != std::string::npos )
        {
          named_ea = fi.start_ea;
          break;
        }
      }
      if ( named_ea == BADADDR )
      {
        std::printf("WASMHEX_ABI_PROBE census %s NOT_FOUND\n", want_function.c_str());
      }
      else
      {
        hexrays_failure_t hf;
        cfuncptr_t cfunc(nullptr);
        try
        {
          cfunc = decompile_function(named_ea, &hf, DECOMP_NO_CACHE);
        }
        catch ( ... )
        {
        }
        if ( cfunc == nullptr )
        {
          std::printf("WASMHEX_ABI_PROBE census %s decompile=FAIL code=%d\n", want_function.c_str(),
                      hf.code);
        }
        else
        {
          const strvec_t &sv = cfunc->get_pseudocode();
          std::string pseudocode;
          for ( size_t k = 0; k < sv.size(); ++k )
          {
            qstring clean;
            tag_remove(&clean, sv[k].line);
            if ( k > 0 )
              pseudocode.push_back('\n');
            pseudocode.append(clean.c_str(), clean.length());
          }
          std::printf(
            "WASMHEX_ABI_PROBE census %s lines=%zu SET_PAIR=%zu PAIR64=%zu LODWORD=%zu HIDWORD=%zu\n",
            want_function.c_str(), split_lines_no_eol(pseudocode).size(),
            count_substr(pseudocode, "__SET_PAIR__"), count_substr(pseudocode, "__PAIR64__"),
            count_substr(pseudocode, "LODWORD"), count_substr(pseudocode, "HIDWORD"));
        }
      }
      sess.close();
      return 0;
    }
    tinfo_t saved_tif;
    bool has_saved = get_tinfo(&saved_tif, probe_ea);
    const auto try_retloc = [&](const char *label, bool single_reg, int single_reg_index)
    {
      func_type_data_t function;
      if ( !parse_decl(&function.rettype, nullptr, get_idati(), "__int64 x;", PT_SIL) )
      {
        std::printf("WASMHEX_ABI_PROBE e2e %s parse_decl failed\n", label);
        return;
      }
      if ( single_reg )
        function.retloc.set_reg1(single_reg_index);
      else
        function.retloc.set_reg2(0, 1);
      function.set_cc(CM_CC_SPECIAL);
      function.flags |= FTI_ARGLOCS | FTI_EXPLOCS;
      tinfo_t tif;
      if ( !tif.create_func(function) )
      {
        std::printf("WASMHEX_ABI_PROBE e2e %s create_func=FAIL\n", label);
        return;
      }
      if ( !apply_tinfo(probe_ea, tif, TINFO_DEFINITE) )
      {
        std::printf("WASMHEX_ABI_PROBE e2e %s apply_tinfo=FAIL\n", label);
        return;
      }
      hexrays_failure_t hf;
      cfuncptr_t cfunc(nullptr);
      std::string failure;
      try
      {
        cfunc = decompile_function(probe_ea, &hf, DECOMP_NO_CACHE);
      }
      catch ( const vd_failure_t &vf )
      {
        failure = "vd_failure:";
        failure += vf.hf.str.c_str();
      }
      catch ( const std::exception &ex )
      {
        failure = std::string("exception:") + ex.what();
      }
      if ( cfunc == nullptr )
      {
        std::printf("WASMHEX_ABI_PROBE e2e %s decompile=FAIL code=%d %s\n", label, hf.code,
                    failure.empty() ? hf.desc().c_str() : failure.c_str());
        return;
      }
      const strvec_t &sv = cfunc->get_pseudocode();
      std::string pseudocode;
      for ( size_t k = 0; k < sv.size(); ++k )
      {
        qstring clean;
        tag_remove(&clean, sv[k].line);
        if ( k > 0 )
          pseudocode.push_back('\n');
        pseudocode.append(clean.c_str(), clean.length());
      }
      const size_t set_pair = count_substr(pseudocode, "__SET_PAIR__");
      const size_t pair64 = count_substr(pseudocode, "__PAIR__");
      const size_t set_pair64 = count_substr(pseudocode, "__SET_PAIR64__");
      std::printf("WASMHEX_ABI_PROBE e2e %s decompile=OK lines=%zu SET_PAIR=%zu PAIR=%zu\n", label,
                  split_lines_no_eol(pseudocode).size(), set_pair + set_pair64, pair64);
      size_t shown = 0;
      for ( const std::string &line : split_lines_no_eol(pseudocode) )
      {
        if ( shown >= 6 )
          break;
        std::printf("WASMHEX_ABI_PROBE e2e %s | %s\n", label, line.c_str());
        ++shown;
      }
    };
    std::printf("WASMHEX_ABI_PROBE e2e target=%s ea=0x%llx\n", probe_name.c_str(),
                static_cast<unsigned long long>(probe_ea));
    try_retloc("reg2_r0_r1_control", false, 0);
    try_retloc("reg1_r0_forced", true, 0);
    // Registers whose mreg has an 8-byte entry in the engine regmap
    // (mreg2reg(m, 8) == reg): probe whether an 8-byte single-register
    // return survives end-to-end there.
    try_retloc("reg1_r44_forced", true, 44);
    try_retloc("reg1_r56_forced", true, 56);
    // Mreg display names at width 8 for the same registers.
    for ( int reg : { 0, 1, 2, 6, 7, 44, 56, 63 } )
    {
      const mreg_t m = reg2mreg(reg);
      qstring name8;
      const int rc8 = m < mr_none ? get_mreg_name(&name8, m, 8) : -1;
      std::printf("WASMHEX_ABI_PROBE mreg_name reg=%d mreg=%d width8=%s rc=%d\n", reg, int(m),
                  name8.empty() ? "(empty)" : name8.c_str(), rc8);
    }
    if ( has_saved )
      (void)apply_tinfo(probe_ea, saved_tif, TINFO_DEFINITE);
  }

  // Section 4: hexrays callback iteration direction
  // Install two markers (A then B), decompile one function, and print the
  // hxe_maturity CMAT_FINAL invocation order. Decides FIFO vs LIFO for the
  // engine, which is what the libc++ remove/reinstall dance depends on.
  {
    static std::vector<std::string> order_log;
    order_log.clear();
    struct marker_cb_t
    {
      static ssize_t idaapi thunk(void *ud, hexrays_event_t event, va_list va)
      {
        if ( event == hxe_maturity )
        {
          cfunc_t *cfunc = va_arg(va, cfunc_t *);
          const int maturity = va_arg(va, int);
          if ( maturity == CMAT_FINAL && cfunc != nullptr )
            order_log.push_back(std::string(static_cast<const char *>(ud)));
        }
        return 0;
      }
    };
    const char *tag_a = "A";
    const char *tag_b = "B";
    const bool installed_a = install_hexrays_callback(marker_cb_t::thunk, (void *)tag_a);
    const bool installed_b = install_hexrays_callback(marker_cb_t::thunk, (void *)tag_b);
    ea_t any_ea = BADADDR;
    {
      func_entry_info_t fi;
      for ( size_t i = 0; i < get_func_qty(); ++i )
        if ( get_func_entry_info_by_num(&fi, i) )
        {
          any_ea = fi.start_ea;
          break;
        }
    }
    if ( installed_a && installed_b && any_ea != BADADDR )
    {
      try
      {
        (void)decompile_function(any_ea, nullptr, DECOMP_NO_CACHE);
      }
      catch ( ... )
      {
      }
    }
    std::string sequence;
    for ( const std::string &tag : order_log )
    {
      if ( !sequence.empty() )
        sequence += ",";
      sequence += tag;
    }
    std::printf("WASMHEX_ABI_PROBE cb_order install=A,B invoke=%s direction=%s\n", sequence.c_str(),
                sequence == "A,B" ? "FIFO" : (sequence == "B,A" ? "LIFO" : "unknown"));
    if ( installed_a )
      remove_hexrays_callback(marker_cb_t::thunk, (void *)tag_a);
    if ( installed_b )
      remove_hexrays_callback(marker_cb_t::thunk, (void *)tag_b);
  }

  sess.close();
  return 0;
}

void print_usage(const char *prog)
{
  std::fprintf(
    stderr,
    "usage:\n"
    "  %s dump-golden [<fixture-dir-or-wasm> ...] [--out-root <dir>] [--idausr <dir>]\n"
    "  %s expect [<golden-root-or-fixture> ...]\n"
    "  %s f5 <module.wasm> [--stride N] [--shard S] [--limit L] [--bounds hidden|explicit]\n"
    "  %s sample [<module.wasm>] [--stride N] [--max-failures M] [--idausr <dir>]\n"
    "  %s matrix [--idausr <dir>]\n"
    "  %s golden-merge [<module.wasm>] [--strict] [--regenerate] [--idausr <dir>]\n"
    "  %s check [<case> ...] [--idausr <dir>]\n"
    "  %s abi-probe <module.wasm> [--idausr <dir>]\n",
    prog, prog, prog, prog, prog, prog, prog, prog);
}

} // anonymous namespace

int main(int argc, char **argv)
{
  // Always enable verify unconditionally at startup
  force_enable_verify_env();

  if ( argc < 2 )
  {
    print_usage(argv[0]);
    return 2;
  }

  const char *mode = argv[1];
  if ( std::strcmp(mode, "expect") == 0 )
    return cmd_expect(argc - 2, argv + 2);
  if ( std::strcmp(mode, "dump-golden") == 0 )
    return cmd_dump_golden(argc - 2, argv + 2);
  if ( std::strcmp(mode, "f5") == 0 )
    return cmd_f5(argc - 2, argv + 2);
  if ( std::strcmp(mode, "sample") == 0 )
    return cmd_sample(argv[0], argc - 2, argv + 2);
  if ( std::strcmp(mode, "matrix") == 0 )
    return cmd_matrix(argc - 2, argv + 2);
  if ( std::strcmp(mode, "golden-merge") == 0 )
    return cmd_golden_merge(argc - 2, argv + 2);
  if ( std::strcmp(mode, "check") == 0 )
    return cmd_check(argc - 2, argv + 2);
  if ( std::strcmp(mode, "abi-probe") == 0 )
    return cmd_abi_probe(argc - 2, argv + 2);

  print_usage(argv[0]);
  return 2;
}
