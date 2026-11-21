// Data-driven C++ WebAssembly runtime oracle (libwasmtime + wabt
// wasm-interp). Usage:
//   runtime_oracle [--cases <cases.tsv>] [--fixtures <fixtures_dir>]
//                  [--scenario <name>] [extra_wasm_paths...]
// Legacy-EH fixtures (pre-standard try/catch/rethrow, rejected by
// libwasmtime) shell out to WABT's wasm-interp and parse its output.
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <sstream>
#include <string>
#include <vector>

#include "wasmtime.h"

namespace
{

struct CaseRow
{
  size_t line_no = 0;
  std::string scenario;
  std::string fixture;
  std::string action;
  std::string target;
  std::string args;
  std::string expect;
  std::string notes;
};

struct HostLog
{
  std::vector<std::string> entries;
};

struct ActiveSession
{
  wasm_engine_t *engine = nullptr;
  wasmtime_store_t *store = nullptr;
  wasmtime_context_t *ctx = nullptr;
  wasmtime_module_t *module = nullptr;
  wasmtime_linker_t *linker = nullptr;
  wasmtime_instance_t instance = {};
  bool has_instance = false;

  bool has_imported_memory = false;
  wasmtime_memory_t imported_memory = {};
  bool has_imported_shared_memory = false;
  wasmtime_sharedmemory_t *imported_shared_memory = nullptr;
  bool has_imported_table = false;
  wasmtime_table_t imported_table = {};

  bool is_wabt_interp = false;
  std::map<std::string, std::string> wabt_results;

  int host_obj1 = 101;
  int host_obj2 = 202;
  HostLog host_log;

  ~ActiveSession()
  {
    reset();
    if ( engine != nullptr )
      wasm_engine_delete(engine);
  }

  void reset()
  {
    if ( imported_shared_memory != nullptr )
    {
      wasmtime_sharedmemory_delete(imported_shared_memory);
      imported_shared_memory = nullptr;
    }
    if ( linker != nullptr )
    {
      wasmtime_linker_delete(linker);
      linker = nullptr;
    }
    if ( module != nullptr )
    {
      wasmtime_module_delete(module);
      module = nullptr;
    }
    if ( store != nullptr )
    {
      wasmtime_store_delete(store);
      store = nullptr;
    }
    ctx = nullptr;
    has_instance = false;
    has_imported_memory = false;
    has_imported_shared_memory = false;
    has_imported_table = false;
    is_wabt_interp = false;
    wabt_results.clear();
    host_log.entries.clear();
  }
};

wasm_engine_t *create_full_engine()
{
  wasm_config_t *cfg = wasm_config_new();
  wasmtime_config_wasm_threads_set(cfg, true);
  wasmtime_config_shared_memory_set(cfg, true);
  wasmtime_config_wasm_tail_call_set(cfg, true);
  wasmtime_config_wasm_reference_types_set(cfg, true);
  wasmtime_config_wasm_function_references_set(cfg, true);
  wasmtime_config_wasm_gc_set(cfg, true);
  wasmtime_config_wasm_simd_set(cfg, true);
  wasmtime_config_wasm_relaxed_simd_set(cfg, true);
  wasmtime_config_wasm_bulk_memory_set(cfg, true);
  wasmtime_config_wasm_multi_value_set(cfg, true);
  wasmtime_config_wasm_multi_memory_set(cfg, true);
  wasmtime_config_wasm_memory64_set(cfg, true);
  wasmtime_config_wasm_exceptions_set(cfg, true);
  return wasm_engine_new_with_config(cfg);
}

std::string format_error(wasmtime_error_t *err)
{
  if ( err == nullptr )
    return "";
  wasm_byte_vec_t msg;
  wasmtime_error_message(err, &msg);
  std::string out(msg.data, msg.size);
  wasm_byte_vec_delete(&msg);
  wasmtime_error_delete(err);
  return out;
}

std::string format_trap(wasm_trap_t *trap)
{
  if ( trap == nullptr )
    return "";
  wasm_message_t msg;
  wasm_trap_message(trap, &msg);
  std::string out(msg.data, msg.size);
  wasm_byte_vec_delete(&msg);
  wasm_trap_delete(trap);
  return out;
}

std::vector<std::string> split_char(const std::string &s, char delim)
{
  std::vector<std::string> out;
  std::stringstream ss(s);
  std::string item;
  while ( std::getline(ss, item, delim) )
    out.push_back(item);
  return out;
}

std::uint64_t parse_u64_bits(const std::string &text)
{
  if ( text.empty() )
    return 0;
  if ( text[0] == '-' )
  {
    const std::int64_t sv = std::strtoll(text.c_str(), nullptr, 0);
    return static_cast<std::uint64_t>(sv);
  }
  return std::strtoull(text.c_str(), nullptr, 0);
}

std::uint32_t parse_u32_bits(const std::string &text)
{
  return static_cast<std::uint32_t>(parse_u64_bits(text));
}

float parse_f32_spec(const std::string &text)
{
  if ( text == "+0.0" || text == "0" || text == "0.0" )
    return 0.0f;
  if ( text == "-0.0" || text == "-0" )
    return -0.0f;
  if ( text == "inf" || text == "+inf" )
    return INFINITY;
  if ( text == "-inf" )
    return -INFINITY;
  if ( text == "nan" )
    return std::nanf("");
  if ( text.rfind("bits:", 0) == 0 )
  {
    const std::uint32_t b = parse_u32_bits(text.substr(5));
    float f = 0.0f;
    std::memcpy(&f, &b, 4);
    return f;
  }
  return std::strtof(text.c_str(), nullptr);
}

double parse_f64_spec(const std::string &text)
{
  if ( text == "+0.0" || text == "0" || text == "0.0" )
    return 0.0;
  if ( text == "-0.0" || text == "-0" )
    return -0.0;
  if ( text == "inf" || text == "+inf" )
    return INFINITY;
  if ( text == "-inf" )
    return -INFINITY;
  if ( text == "nan" )
    return std::nan("");
  if ( text.rfind("bits:", 0) == 0 )
  {
    const std::uint64_t b = parse_u64_bits(text.substr(5));
    double d = 0.0;
    std::memcpy(&d, &b, 8);
    return d;
  }
  return std::strtod(text.c_str(), nullptr);
}

// Host callbacks
wasm_trap_t *cb_identity(void * /*env*/, wasmtime_caller_t * /*caller*/, const wasmtime_val_t *args,
                         size_t nargs, wasmtime_val_t *results, size_t nresults)
{
  if ( nargs >= 1 && nresults >= 1 )
    wasmtime_val_clone(&args[0], &results[0]);
  return nullptr;
}

wasm_trap_t *cb_pair_10_20(void * /*env*/, wasmtime_caller_t * /*caller*/,
                           const wasmtime_val_t *args, size_t nargs, wasmtime_val_t *results,
                           size_t nresults)
{
  if ( nargs >= 1 && nresults >= 2 )
  {
    const std::int32_t v = args[0].of.i32;
    results[0].kind = WASMTIME_I32;
    results[0].of.i32 = v + 10;
    results[1].kind = WASMTIME_I32;
    results[1].of.i32 = v + 20;
  }
  return nullptr;
}

wasm_trap_t *cb_calls_multi_pair(void *env, wasmtime_caller_t * /*caller*/,
                                 const wasmtime_val_t *args, size_t nargs, wasmtime_val_t *results,
                                 size_t nresults)
{
  auto *log = static_cast<HostLog *>(env);
  if ( nargs >= 1 && nresults >= 2 )
  {
    const std::int32_t v = args[0].of.i32;
    if ( log != nullptr )
      log->entries.push_back("pair:" + std::to_string(v));
    results[0].kind = WASMTIME_I32;
    results[0].of.i32 = v;
    results[1].kind = WASMTIME_I32;
    results[1].of.i32 = v + 1;
  }
  return nullptr;
}

wasm_trap_t *cb_calls_multi_six(void *env, wasmtime_caller_t * /*caller*/,
                                const wasmtime_val_t *args, size_t nargs, wasmtime_val_t *results,
                                size_t nresults)
{
  auto *log = static_cast<HostLog *>(env);
  if ( nargs >= 1 && nresults >= 5 )
  {
    const std::int32_t v = args[0].of.i32;
    if ( log != nullptr )
      log->entries.push_back("six:" + std::to_string(v));
    results[0].kind = WASMTIME_I32;
    results[0].of.i32 = v;
    results[1].kind = WASMTIME_I64;
    results[1].of.i64 = static_cast<std::int64_t>(v) + 1;
    results[2].kind = WASMTIME_I32;
    results[2].of.i32 = v + 2;
    results[3].kind = WASMTIME_I32;
    results[3].of.i32 = v + 3;
    results[4].kind = WASMTIME_I32;
    results[4].of.i32 = v + 4;
  }
  return nullptr;
}

wasm_trap_t *cb_calls_multi_effect(void *env, wasmtime_caller_t * /*caller*/,
                                   const wasmtime_val_t *args, size_t nargs,
                                   wasmtime_val_t *results, size_t nresults)
{
  auto *log = static_cast<HostLog *>(env);
  if ( nargs >= 1 && nresults >= 1 )
  {
    const std::int32_t v = args[0].of.i32;
    if ( log != nullptr )
      log->entries.push_back("effect:" + std::to_string(v));
    results[0].kind = WASMTIME_I32;
    results[0].of.i32 = v;
  }
  return nullptr;
}

std::string find_executable(const char *name, const char *brew_fallback)
{
  if ( std::filesystem::exists(brew_fallback) )
    return brew_fallback;
  return name;
}

bool load_wasm_or_wat_bytes(const std::filesystem::path &resolved_path,
                            std::vector<std::uint8_t> &out_bytes, std::string &err_msg)
{
  if ( !std::filesystem::exists(resolved_path) )
  {
    err_msg = "file not found: " + resolved_path.string();
    return false;
  }
  if ( resolved_path.extension() == ".wat" )
  {
    const auto stamp = static_cast<long long>(
      std::filesystem::last_write_time(resolved_path).time_since_epoch().count());
    const std::filesystem::path cache_dir
      = std::filesystem::temp_directory_path() / "wasmhex_rt_oracle_cache";
    std::filesystem::create_directories(cache_dir);
    const std::filesystem::path cached_wasm
      = cache_dir / (resolved_path.stem().string() + "_" + std::to_string(stamp) + ".wasm");
    if ( !std::filesystem::exists(cached_wasm) )
    {
      const std::string wat2wasm = find_executable("wat2wasm", "/opt/homebrew/bin/wat2wasm");
      const std::string stem = resolved_path.stem().string();
      std::string flags;
      if ( stem.find("growable_memory32") != std::string::npos
           && stem.find("exported") == std::string::npos
           && stem.find("imported") == std::string::npos )
      {
        flags = "--enable-threads ";
      }
      else if ( stem.find("call_ref") != std::string::npos
                || stem.find("sparse_reference") != std::string::npos )
      {
        flags = "--enable-function-references ";
      }
      const std::string cmd = wat2wasm + " " + flags + "\"" + resolved_path.string() + "\" -o \""
        + cached_wasm.string() + "\"";
      if ( std::system(cmd.c_str()) != 0 ) // NOLINT(bugprone-command-processor)
      {
        err_msg = "wat2wasm failed: " + cmd;
        return false;
      }
    }
    std::ifstream in(cached_wasm, std::ios::binary);
    out_bytes.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    return true;
  }
  std::ifstream in(resolved_path, std::ios::binary);
  out_bytes.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
  return true;
}

std::filesystem::path resolve_fixture_spec(const std::string &spec,
                                           const std::filesystem::path &fixtures_dir,
                                           const std::vector<std::string> &extra_args)
{
  std::string default_name = spec;
  if ( !spec.empty() && spec[0] == '$' )
  {
    const size_t eq = spec.find('=');
    const std::string idx_str = (eq == std::string::npos) ? spec.substr(1) : spec.substr(1, eq - 1);
    const size_t idx = static_cast<size_t>(std::strtoul(idx_str.c_str(), nullptr, 10));
    if ( idx >= 1 && idx <= extra_args.size() )
      return std::filesystem::path(extra_args[idx - 1]);
    if ( eq != std::string::npos )
      default_name = spec.substr(eq + 1);
  }
  std::filesystem::path p(default_name);
  if ( p.is_absolute() && std::filesystem::exists(p) )
    return p;
  if ( std::filesystem::exists(fixtures_dir / p) )
    return fixtures_dir / p;
  if ( p.extension() == ".wasm" )
  {
    std::filesystem::path wat_p = fixtures_dir / p.stem();
    wat_p += ".wat";
    if ( std::filesystem::exists(wat_p) )
      return wat_p;
  }
  return fixtures_dir / p;
}

bool setup_import_profile(ActiveSession &sess, const std::string &profile, std::string &err_msg)
{
  if ( profile == "-" || profile.empty() )
    return true;

  if ( profile == "env_identity" )
  {
    wasm_importtype_vec_t imports;
    wasmtime_module_imports(sess.module, &imports);
    for ( size_t i = 0; i < imports.size; ++i )
    {
      const wasm_name_t *mod_name = wasm_importtype_module(imports.data[i]);
      const wasm_name_t *fn_name = wasm_importtype_name(imports.data[i]);
      const wasm_externtype_t *ext_ty = wasm_importtype_type(imports.data[i]);
      if ( wasm_externtype_kind(ext_ty) == WASM_EXTERN_FUNC )
      {
        const wasm_functype_t *fty = wasm_externtype_as_functype_const(ext_ty);
        wasmtime_error_t *err
          = wasmtime_linker_define_func(sess.linker, mod_name->data, mod_name->size, fn_name->data,
                                        fn_name->size, fty, cb_identity, nullptr, nullptr);
        if ( err != nullptr )
        {
          wasm_importtype_vec_delete(&imports);
          err_msg = format_error(err);
          return false;
        }
      }
    }
    wasm_importtype_vec_delete(&imports);
    return true;
  }

  if ( profile == "env_pair_10_20" )
  {
    wasm_functype_t *fty = wasm_functype_new_1_2(wasm_valtype_new_i32(), wasm_valtype_new_i32(),
                                                 wasm_valtype_new_i32());
    wasmtime_error_t *err = wasmtime_linker_define_func(sess.linker, "env", 3, "pair", 4, fty,
                                                        cb_pair_10_20, nullptr, nullptr);
    wasm_functype_delete(fty);
    if ( err != nullptr )
    {
      err_msg = format_error(err);
      return false;
    }
    return true;
  }

  if ( profile == "env_calls_multi" )
  {
    wasm_functype_t *pair_ty = wasm_functype_new_1_2(wasm_valtype_new_i32(), wasm_valtype_new_i32(),
                                                     wasm_valtype_new_i32());
    wasmtime_error_t *err = wasmtime_linker_define_func(
      sess.linker, "env", 3, "pair", 4, pair_ty, cb_calls_multi_pair, &sess.host_log, nullptr);
    wasm_functype_delete(pair_ty);
    if ( err != nullptr )
    {
      err_msg = format_error(err);
      return false;
    }

    wasm_valtype_t *ps[1] = { wasm_valtype_new_i32() };
    wasm_valtype_t *rs[5] = {
      wasm_valtype_new_i32(), wasm_valtype_new_i64(), wasm_valtype_new_i32(),
      wasm_valtype_new_i32(), wasm_valtype_new_i32(),
    };
    wasm_valtype_vec_t params, results;
    wasm_valtype_vec_new(&params, 1, ps);
    wasm_valtype_vec_new(&results, 5, rs);
    wasm_functype_t *six_ty = wasm_functype_new(&params, &results);
    err = wasmtime_linker_define_func(sess.linker, "env", 3, "six", 3, six_ty, cb_calls_multi_six,
                                      &sess.host_log, nullptr);
    wasm_functype_delete(six_ty);
    if ( err != nullptr )
    {
      err_msg = format_error(err);
      return false;
    }

    wasm_functype_t *eff_ty = wasm_functype_new_1_1(wasm_valtype_new_i32(), wasm_valtype_new_i32());
    err = wasmtime_linker_define_func(sess.linker, "env", 3, "effect", 6, eff_ty,
                                      cb_calls_multi_effect, &sess.host_log, nullptr);
    wasm_functype_delete(eff_ty);
    if ( err != nullptr )
    {
      err_msg = format_error(err);
      return false;
    }
    return true;
  }

  if ( profile == "env_memory_1_3" )
  {
    wasm_memorytype_t *mty = nullptr;
    wasmtime_error_t *err = wasmtime_memorytype_new(1, true, 3, false, false, 16, &mty);
    if ( err != nullptr )
    {
      err_msg = format_error(err);
      return false;
    }
    err = wasmtime_memory_new(sess.ctx, mty, &sess.imported_memory);
    wasm_memorytype_delete(mty);
    if ( err != nullptr )
    {
      err_msg = format_error(err);
      return false;
    }
    sess.has_imported_memory = true;
    wasmtime_extern_t item;
    item.kind = WASMTIME_EXTERN_MEMORY;
    item.of.memory = sess.imported_memory;
    err = wasmtime_linker_define(sess.linker, sess.ctx, "env", 3, "memory", 6, &item);
    if ( err != nullptr )
    {
      err_msg = format_error(err);
      return false;
    }
    return true;
  }

  if ( profile == "env_memory_shared_1_1" )
  {
    wasm_memorytype_t *mty = nullptr;
    wasmtime_error_t *err = wasmtime_memorytype_new(1, true, 1, false, true, 16, &mty);
    if ( err != nullptr )
    {
      err_msg = format_error(err);
      return false;
    }
    err = wasmtime_sharedmemory_new(sess.engine, mty, &sess.imported_shared_memory);
    wasm_memorytype_delete(mty);
    if ( err != nullptr )
    {
      err_msg = format_error(err);
      return false;
    }
    sess.has_imported_shared_memory = true;
    wasmtime_extern_t item;
    item.kind = WASMTIME_EXTERN_SHAREDMEMORY;
    item.of.sharedmemory = sess.imported_shared_memory;
    err = wasmtime_linker_define(sess.linker, sess.ctx, "env", 3, "memory", 6, &item);
    if ( err != nullptr )
    {
      err_msg = format_error(err);
      return false;
    }
    return true;
  }

  if ( profile == "env_imported_table_3" || profile == "env_table_1_offset_2"
       || profile == "env_table_4_offset_2" )
  {
    const uint32_t tsize = (profile == "env_imported_table_3") ? 3
      : (profile == "env_table_4_offset_2")                    ? 4
                                                               : 1;
    const char *tname = (profile == "env_imported_table_3") ? "imported" : "table";
    wasm_limits_t lim = { tsize, tsize };
    wasm_tabletype_t *tty = wasm_tabletype_new(wasm_valtype_new_funcref(), &lim);
    wasmtime_val_t init_val = {};
    init_val.kind = WASMTIME_FUNCREF;
    wasmtime_funcref_set_null(&init_val.of.funcref);
    wasmtime_error_t *err = wasmtime_table_new(sess.ctx, tty, &init_val, &sess.imported_table);
    wasm_tabletype_delete(tty);
    if ( err != nullptr )
    {
      err_msg = format_error(err);
      return false;
    }
    sess.has_imported_table = true;
    wasmtime_extern_t t_item;
    t_item.kind = WASMTIME_EXTERN_TABLE;
    t_item.of.table = sess.imported_table;
    err
      = wasmtime_linker_define(sess.linker, sess.ctx, "env", 3, tname, std::strlen(tname), &t_item);
    if ( err != nullptr )
    {
      err_msg = format_error(err);
      return false;
    }

    if ( profile != "env_imported_table_3" )
    {
      wasm_globaltype_t *gty = wasm_globaltype_new(wasm_valtype_new_i32(), WASM_CONST);
      wasmtime_val_t gval = {};
      gval.kind = WASMTIME_I32;
      gval.of.i32 = 2;
      wasmtime_global_t glob = {};
      err = wasmtime_global_new(sess.ctx, gty, &gval, &glob);
      wasm_globaltype_delete(gty);
      if ( err != nullptr )
      {
        err_msg = format_error(err);
        return false;
      }
      wasmtime_extern_t g_item;
      g_item.kind = WASMTIME_EXTERN_GLOBAL;
      g_item.of.global = glob;
      err = wasmtime_linker_define(sess.linker, sess.ctx, "env", 3, "offset", 6, &g_item);
      if ( err != nullptr )
      {
        err_msg = format_error(err);
        return false;
      }
    }
    return true;
  }

  if ( profile == "table64_pair" )
  {
    const char *wat
      = "(module (table (export \"t\") i64 0 20 funcref) (table (export \"second\") i64 0 20 funcref))";
    wasm_byte_vec_t wasm_bytes;
    wasmtime_error_t *err = wasmtime_wat2wasm(wat, std::strlen(wat), &wasm_bytes);
    if ( err != nullptr )
    {
      err_msg = format_error(err);
      return false;
    }
    wasmtime_module_t *hmod = nullptr;
    err = wasmtime_module_new(sess.engine, reinterpret_cast<const uint8_t *>(wasm_bytes.data),
                              wasm_bytes.size, &hmod);
    wasm_byte_vec_delete(&wasm_bytes);
    if ( err != nullptr )
    {
      err_msg = format_error(err);
      return false;
    }
    wasmtime_instance_t hinst = {};
    wasm_trap_t *trap = nullptr;
    err = wasmtime_instance_new(sess.ctx, hmod, nullptr, 0, &hinst, &trap);
    wasmtime_module_delete(hmod);
    if ( trap != nullptr || err != nullptr )
    {
      err_msg = trap ? format_trap(trap) : format_error(err);
      return false;
    }
    wasmtime_extern_t t_ext = {}, s_ext = {};
    wasmtime_instance_export_get(sess.ctx, &hinst, "t", 1, &t_ext);
    wasmtime_instance_export_get(sess.ctx, &hinst, "second", 6, &s_ext);
    err = wasmtime_linker_define(sess.linker, sess.ctx, "env", 3, "t", 1, &t_ext);
    if ( err == nullptr )
      err = wasmtime_linker_define(sess.linker, sess.ctx, "env", 3, "second", 6, &s_ext);
    if ( err != nullptr )
    {
      err_msg = format_error(err);
      return false;
    }
    return true;
  }

  err_msg = "unknown import profile: " + profile;
  return false;
}

bool run_wabt_interp(ActiveSession &sess, const std::filesystem::path &wasm_path,
                     std::string &err_msg)
{
  const std::string interp = find_executable("wasm-interp", "/opt/homebrew/bin/wasm-interp");
  const std::string cmd = interp + " --enable-all --run-all-exports \"" + wasm_path.string() + "\"";
  FILE *pipe = popen(cmd.c_str(), "r"); // NOLINT(bugprone-command-processor)
  if ( pipe == nullptr )
  {
    err_msg = "popen failed for wasm-interp";
    return false;
  }
  char line[512];
  while ( std::fgets(line, sizeof(line), pipe) != nullptr )
  {
    std::string s(line);
    while ( !s.empty() && (s.back() == '\n' || s.back() == '\r') )
      s.pop_back();
    const size_t arrow = s.find("() => ");
    if ( arrow != std::string::npos )
    {
      const std::string fn = s.substr(0, arrow);
      const std::string res = s.substr(arrow + 6);
      sess.wabt_results[fn] = res;
    }
  }
  const int rc = pclose(pipe);
  if ( rc != 0 )
  {
    err_msg = "wasm-interp exited with non-zero status";
    return false;
  }
  sess.is_wabt_interp = true;
  return true;
}

bool get_active_memory(ActiveSession &sess, uint8_t **data_out, size_t *size_out,
                       std::string &err_msg)
{
  wasmtime_extern_t item = {};
  if ( wasmtime_instance_export_get(sess.ctx, &sess.instance, "memory", 6, &item) )
  {
    if ( item.kind == WASMTIME_EXTERN_MEMORY )
    {
      *data_out = wasmtime_memory_data(sess.ctx, &item.of.memory);
      *size_out = wasmtime_memory_data_size(sess.ctx, &item.of.memory);
      wasmtime_extern_delete(&item);
      return true;
    }
    if ( item.kind == WASMTIME_EXTERN_SHAREDMEMORY )
    {
      *data_out = wasmtime_sharedmemory_data(item.of.sharedmemory);
      *size_out = wasmtime_sharedmemory_data_size(item.of.sharedmemory);
      wasmtime_extern_delete(&item);
      return true;
    }
    wasmtime_extern_delete(&item);
  }
  if ( sess.has_imported_memory )
  {
    *data_out = wasmtime_memory_data(sess.ctx, &sess.imported_memory);
    *size_out = wasmtime_memory_data_size(sess.ctx, &sess.imported_memory);
    return true;
  }
  if ( sess.has_imported_shared_memory )
  {
    *data_out = wasmtime_sharedmemory_data(sess.imported_shared_memory);
    *size_out = wasmtime_sharedmemory_data_size(sess.imported_shared_memory);
    return true;
  }
  err_msg = "no active memory found on instance";
  return false;
}

bool build_arg_val(ActiveSession &sess, const std::string &token, wasmtime_val_t &out,
                   std::string &err_msg)
{
  const size_t colon = token.find(':');
  if ( colon == std::string::npos )
  {
    err_msg = "malformed arg token: " + token;
    return false;
  }
  const std::string type = token.substr(0, colon);
  const std::string val = token.substr(colon + 1);
  if ( type == "i32" )
  {
    out.kind = WASMTIME_I32;
    out.of.i32 = static_cast<std::int32_t>(parse_u32_bits(val));
    return true;
  }
  if ( type == "i64" )
  {
    out.kind = WASMTIME_I64;
    out.of.i64 = static_cast<std::int64_t>(parse_u64_bits(val));
    return true;
  }
  if ( type == "f32" )
  {
    out.kind = WASMTIME_F32;
    out.of.f32 = parse_f32_spec(val);
    return true;
  }
  if ( type == "f64" )
  {
    out.kind = WASMTIME_F64;
    out.of.f64 = parse_f64_spec(val);
    return true;
  }
  if ( type == "externref" )
  {
    out.kind = WASMTIME_EXTERNREF;
    if ( val == "null" )
    {
      wasmtime_externref_set_null(&out.of.externref);
      return true;
    }
    void *ptr = (val == "obj2") ? static_cast<void *>(&sess.host_obj2)
                                : static_cast<void *>(&sess.host_obj1);
    if ( !wasmtime_externref_new(sess.ctx, ptr, nullptr, &out.of.externref) )
    {
      err_msg = "wasmtime_externref_new failed";
      return false;
    }
    return true;
  }
  if ( type == "funcref" )
  {
    out.kind = WASMTIME_FUNCREF;
    if ( val == "null" )
    {
      wasmtime_funcref_set_null(&out.of.funcref);
      return true;
    }
    wasmtime_extern_t item = {};
    if ( !wasmtime_instance_export_get(sess.ctx, &sess.instance, val.c_str(), val.size(), &item)
         || item.kind != WASMTIME_EXTERN_FUNC )
    {
      err_msg = "funcref export not found: " + val;
      return false;
    }
    out.of.funcref = item.of.func;
    return true;
  }
  err_msg = "unsupported arg type: " + type;
  return false;
}

bool check_single_result(ActiveSession &sess, const wasmtime_val_t &actual,
                         const std::string &expect_token, std::string &err_msg)
{
  const size_t colon = expect_token.find(':');
  if ( colon == std::string::npos )
  {
    err_msg = "malformed expect token: " + expect_token;
    return false;
  }
  const std::string type = expect_token.substr(0, colon);
  const std::string val = expect_token.substr(colon + 1);

  if ( type == "i32" )
  {
    if ( actual.kind != WASMTIME_I32 )
    {
      err_msg = "expected i32 result";
      return false;
    }
    const auto bits = static_cast<std::uint32_t>(actual.of.i32);
    if ( val == "nan32" )
    {
      if ( (bits & 0x7f800000U) != 0x7f800000U || (bits & 0x007fffffU) == 0 )
      {
        err_msg = "expected f32 NaN bits, got " + std::to_string(bits);
        return false;
      }
      return true;
    }
    if ( val == "qnan32" )
    {
      if ( (bits & 0x7f800000U) != 0x7f800000U || (bits & 0x00400000U) == 0
           || (bits & 0x007fffffU) == 0 )
      {
        char buf[64];
        std::snprintf(buf, sizeof(buf), "0x%08x", bits);
        err_msg = std::string("expected f32 quiet NaN bits, got ") + buf;
        return false;
      }
      return true;
    }
    const std::uint32_t exp_bits = parse_u32_bits(val);
    if ( bits != exp_bits )
    {
      char buf[128];
      std::snprintf(buf, sizeof(buf), "i32 mismatch: got %d (0x%08x), expected %d (0x%08x)",
                    actual.of.i32, bits, static_cast<std::int32_t>(exp_bits), exp_bits);
      err_msg = buf;
      return false;
    }
    return true;
  }

  if ( type == "i64" )
  {
    if ( actual.kind != WASMTIME_I64 )
    {
      err_msg = "expected i64 result";
      return false;
    }
    const auto bits = static_cast<std::uint64_t>(actual.of.i64);
    if ( val == "nan64" )
    {
      if ( (bits & 0x7ff0000000000000ULL) != 0x7ff0000000000000ULL
           || (bits & 0x000fffffffffffffULL) == 0 )
      {
        err_msg = "expected f64 NaN bits";
        return false;
      }
      return true;
    }
    if ( val == "qnan64" )
    {
      if ( (bits & 0x7ff0000000000000ULL) != 0x7ff0000000000000ULL
           || (bits & 0x0008000000000000ULL) == 0 || (bits & 0x000fffffffffffffULL) == 0 )
      {
        err_msg = "expected f64 quiet NaN bits";
        return false;
      }
      return true;
    }
    const std::uint64_t exp_bits = parse_u64_bits(val);
    if ( bits != exp_bits )
    {
      char buf[128];
      std::snprintf(buf, sizeof(buf), "i64 mismatch: got 0x%016llx, expected 0x%016llx",
                    static_cast<unsigned long long>(bits),
                    static_cast<unsigned long long>(exp_bits));
      err_msg = buf;
      return false;
    }
    return true;
  }

  if ( type == "f32" )
  {
    if ( actual.kind != WASMTIME_F32 )
    {
      err_msg = "expected f32 result";
      return false;
    }
    std::uint32_t actual_bits = 0;
    std::memcpy(&actual_bits, &actual.of.f32, 4);
    if ( val == "any" )
      return true;
    if ( val == "nan" )
    {
      if ( !std::isnan(actual.of.f32) )
      {
        err_msg = "expected f32 NaN";
        return false;
      }
      return true;
    }
    if ( val == "+0.0" )
    {
      if ( actual_bits != 0x00000000U )
      {
        err_msg = "expected +0.0f";
        return false;
      }
      return true;
    }
    if ( val == "-0.0" )
    {
      if ( actual_bits != 0x80000000U )
      {
        err_msg = "expected -0.0f";
        return false;
      }
      return true;
    }
    const float exp_f = parse_f32_spec(val);
    if ( actual.of.f32 != exp_f )
    {
      err_msg = "f32 value mismatch";
      return false;
    }
    return true;
  }

  if ( type == "f64" )
  {
    if ( actual.kind != WASMTIME_F64 )
    {
      err_msg = "expected f64 result";
      return false;
    }
    std::uint64_t actual_bits = 0;
    std::memcpy(&actual_bits, &actual.of.f64, 8);
    if ( val == "any" )
      return true;
    if ( val == "nan" )
    {
      if ( !std::isnan(actual.of.f64) )
      {
        err_msg = "expected f64 NaN";
        return false;
      }
      return true;
    }
    if ( val == "+0.0" )
    {
      if ( actual_bits != 0x0000000000000000ULL )
      {
        err_msg = "expected +0.0";
        return false;
      }
      return true;
    }
    if ( val == "-0.0" )
    {
      if ( actual_bits != 0x8000000000000000ULL )
      {
        err_msg = "expected -0.0";
        return false;
      }
      return true;
    }
    const double exp_d = parse_f64_spec(val);
    if ( actual.of.f64 != exp_d )
    {
      err_msg = "f64 value mismatch";
      return false;
    }
    return true;
  }

  if ( type == "externref" )
  {
    if ( actual.kind != WASMTIME_EXTERNREF )
    {
      err_msg = "expected externref result";
      return false;
    }
    if ( val == "null" )
    {
      if ( !wasmtime_externref_is_null(&actual.of.externref) )
      {
        err_msg = "expected null externref";
        return false;
      }
      return true;
    }
    if ( wasmtime_externref_is_null(&actual.of.externref) )
    {
      err_msg = "expected non-null externref, got null";
      return false;
    }
    void *data = wasmtime_externref_data(sess.ctx, &actual.of.externref);
    void *exp_ptr = (val == "obj2") ? static_cast<void *>(&sess.host_obj2)
                                    : static_cast<void *>(&sess.host_obj1);
    if ( data != exp_ptr )
    {
      err_msg = "externref identity mismatch";
      return false;
    }
    return true;
  }

  if ( type == "funcref" )
  {
    if ( actual.kind != WASMTIME_FUNCREF )
    {
      err_msg = "expected funcref result";
      return false;
    }
    if ( val == "null" )
    {
      if ( !wasmtime_funcref_is_null(&actual.of.funcref) )
      {
        err_msg = "expected null funcref";
        return false;
      }
      return true;
    }
    if ( wasmtime_funcref_is_null(&actual.of.funcref) )
    {
      err_msg = "expected non-null funcref " + val + ", got null";
      return false;
    }
    wasmtime_extern_t exp_item = {};
    if ( !wasmtime_instance_export_get(sess.ctx, &sess.instance, val.c_str(), val.size(), &exp_item)
         || exp_item.kind != WASMTIME_EXTERN_FUNC )
    {
      err_msg = "expected funcref export not found: " + val;
      return false;
    }
    void *raw_actual = wasmtime_func_to_raw(sess.ctx, &actual.of.funcref);
    void *raw_exp = wasmtime_func_to_raw(sess.ctx, &exp_item.of.func);
    wasmtime_extern_delete(&exp_item);
    if ( raw_actual != raw_exp )
    {
      err_msg = "funcref identity mismatch for export " + val;
      return false;
    }
    return true;
  }

  if ( type == "i31_s" )
  {
    if ( actual.kind != WASMTIME_ANYREF )
    {
      err_msg = "expected anyref/i31ref";
      return false;
    }
    std::int32_t dst = 0;
    if ( !wasmtime_anyref_i31_get_s(sess.ctx, &actual.of.anyref, &dst) )
    {
      err_msg = "anyref is not an i31ref";
      return false;
    }
    const auto exp = static_cast<std::int32_t>(parse_u32_bits(val));
    if ( dst != exp )
    {
      err_msg = "i31_s mismatch: got " + std::to_string(dst) + ", expected " + std::to_string(exp);
      return false;
    }
    return true;
  }

  err_msg = "unsupported expect type: " + type;
  return false;
}

bool execute_func_and_check(ActiveSession &sess, const wasmtime_func_t &func,
                            const std::string &args_spec, const std::string &expect_spec,
                            std::string &err_msg)
{
  wasm_functype_t *fty = wasmtime_func_type(sess.ctx, &func);
  const wasm_valtype_vec_t *results_ty = wasm_functype_results(fty);
  const size_t nresults = results_ty->size;
  wasm_functype_delete(fty);

  std::vector<wasmtime_val_t> args;
  if ( args_spec != "-" && !args_spec.empty() )
  {
    const auto tokens = split_char(args_spec, ',');
    args.resize(tokens.size());
    for ( size_t i = 0; i < tokens.size(); ++i )
    {
      std::memset(&args[i], 0, sizeof(wasmtime_val_t));
      if ( !build_arg_val(sess, tokens[i], args[i], err_msg) )
      {
        for ( size_t j = 0; j < i; ++j )
          wasmtime_val_unroot(&args[j]);
        return false;
      }
    }
  }

  std::vector<wasmtime_val_t> results(nresults);
  for ( size_t i = 0; i < nresults; ++i )
    std::memset(&results[i], 0, sizeof(wasmtime_val_t));

  wasm_trap_t *trap = nullptr;
  wasmtime_error_t *err
    = wasmtime_func_call(sess.ctx, &func, args.empty() ? nullptr : args.data(), args.size(),
                         results.empty() ? nullptr : results.data(), nresults, &trap);

  for ( auto &a : args )
    wasmtime_val_unroot(&a);

  const bool trapped = (trap != nullptr || err != nullptr);
  const std::string trap_detail = (trap != nullptr) ? format_trap(trap) : format_error(err);

  if ( expect_spec == "TRAP" )
  {
    for ( auto &r : results )
      wasmtime_val_unroot(&r);
    if ( !trapped )
    {
      err_msg = "expected TRAP, but call succeeded";
      return false;
    }
    return true;
  }

  if ( trapped )
  {
    err_msg = "unexpected trap/error: " + trap_detail;
    return false;
  }

  if ( expect_spec == "OK" || expect_spec == "void" )
  {
    for ( auto &r : results )
      wasmtime_val_unroot(&r);
    return true;
  }

  const auto exp_tokens = split_char(expect_spec, ',');
  if ( exp_tokens.size() != nresults )
  {
    for ( auto &r : results )
      wasmtime_val_unroot(&r);
    err_msg = "result arity mismatch: got " + std::to_string(nresults) + ", expected "
      + std::to_string(exp_tokens.size());
    return false;
  }

  bool ok = true;
  for ( size_t i = 0; i < nresults; ++i )
  {
    if ( !check_single_result(sess, results[i], exp_tokens[i], err_msg) )
    {
      ok = false;
      break;
    }
  }

  for ( auto &r : results )
    wasmtime_val_unroot(&r);
  return ok;
}

bool execute_row(ActiveSession &sess, const CaseRow &row, const std::filesystem::path &fixtures_dir,
                 const std::vector<std::string> &extra_args, std::string &err_msg)
{
  if ( row.action == "VALIDATE" )
  {
    const auto path = resolve_fixture_spec(row.fixture, fixtures_dir, extra_args);
    std::vector<std::uint8_t> bytes;
    if ( !load_wasm_or_wat_bytes(path, bytes, err_msg) )
      return false;
    wasmtime_error_t *err = wasmtime_module_validate(sess.engine, bytes.data(), bytes.size());
    const bool is_valid = (err == nullptr);
    if ( err != nullptr )
      wasmtime_error_delete(err);
    const bool exp_valid = (row.expect == "true" || row.expect == "VALID");
    if ( is_valid != exp_valid )
    {
      err_msg = "VALIDATE mismatch on " + path.string();
      return false;
    }
    return true;
  }

  if ( row.action == "VALIDATE_EXTRA" )
  {
    // If extra argv files from index 2 onwards were passed, validate all of them;
    // otherwise validate the default fixtures for this scenario.
    std::vector<std::filesystem::path> targets;
    if ( extra_args.size() >= 2 )
    {
      for ( size_t i = 1; i < extra_args.size(); ++i )
        targets.emplace_back(extra_args[i]);
    }
    else
    {
      targets.push_back(fixtures_dir / "wasmhex_call_ref_typed_result_unsupported.wat");
      targets.push_back(fixtures_dir / "wasmhex_call_ref_vector_result_unsupported.wat");
      targets.push_back(fixtures_dir / "wasmhex_call_ref_two_handles_unsupported.wat");
    }
    const bool exp_valid = (row.expect == "true" || row.expect == "VALID");
    for ( const auto &t : targets )
    {
      std::vector<std::uint8_t> bytes;
      if ( !load_wasm_or_wat_bytes(t, bytes, err_msg) )
        return false;
      wasmtime_error_t *err = wasmtime_module_validate(sess.engine, bytes.data(), bytes.size());
      const bool is_valid = (err == nullptr);
      if ( err != nullptr )
        wasmtime_error_delete(err);
      if ( is_valid != exp_valid )
      {
        err_msg = "VALIDATE_EXTRA mismatch on " + t.string();
        return false;
      }
    }
    return true;
  }

  if ( row.action == "LOAD" )
  {
    sess.reset();
    const auto path = resolve_fixture_spec(row.fixture, fixtures_dir, extra_args);
    if ( row.target == "wabt_interp" )
      return run_wabt_interp(sess, path, err_msg);

    std::vector<std::uint8_t> bytes;
    if ( !load_wasm_or_wat_bytes(path, bytes, err_msg) )
      return false;

    sess.store = wasmtime_store_new(sess.engine, nullptr, nullptr);
    sess.ctx = wasmtime_store_context(sess.store);
    sess.linker = wasmtime_linker_new(sess.engine);

    wasmtime_error_t *err
      = wasmtime_module_new(sess.engine, bytes.data(), bytes.size(), &sess.module);
    if ( err != nullptr )
    {
      err_msg = "module_new (" + path.filename().string() + "): " + format_error(err);
      return false;
    }

    if ( !setup_import_profile(sess, row.target, err_msg) )
      return false;

    wasm_trap_t *trap = nullptr;
    err = wasmtime_linker_instantiate(sess.linker, sess.ctx, sess.module, &sess.instance, &trap);
    if ( trap != nullptr || err != nullptr )
    {
      err_msg = "instantiate (" + path.filename().string()
        + "): " + (trap ? format_trap(trap) : format_error(err));
      return false;
    }
    sess.has_instance = true;
    return true;
  }

  if ( row.action == "CALL" || row.action == "CALL_IF_EXPORTED" )
  {
    if ( sess.is_wabt_interp )
    {
      const auto it = sess.wabt_results.find(row.target);
      if ( it == sess.wabt_results.end() )
      {
        err_msg = "wasm-interp export result not found: " + row.target;
        return false;
      }
      if ( it->second != row.expect )
      {
        err_msg = "wasm-interp mismatch for " + row.target + ": got " + it->second + ", expected "
          + row.expect;
        return false;
      }
      return true;
    }

    wasmtime_extern_t item = {};
    const bool found = wasmtime_instance_export_get(sess.ctx, &sess.instance, row.target.c_str(),
                                                    row.target.size(), &item);
    if ( !found )
    {
      if ( row.action == "CALL_IF_EXPORTED" )
        return true;
      err_msg = "export not found: " + row.target;
      return false;
    }
    if ( item.kind != WASMTIME_EXTERN_FUNC )
    {
      wasmtime_extern_delete(&item);
      if ( row.action == "CALL_IF_EXPORTED" )
        return true;
      err_msg = "export is not a function: " + row.target;
      return false;
    }
    const wasmtime_func_t fn = item.of.func;
    wasmtime_extern_delete(&item);
    return execute_func_and_check(sess, fn, row.args, row.expect, err_msg);
  }

  if ( row.action == "TABLE_SIZE" )
  {
    wasmtime_extern_t item = {};
    if ( !wasmtime_instance_export_get(sess.ctx, &sess.instance, row.target.c_str(),
                                       row.target.size(), &item)
         || item.kind != WASMTIME_EXTERN_TABLE )
    {
      err_msg = "table export not found: " + row.target;
      return false;
    }
    const uint64_t sz = wasmtime_table_size(sess.ctx, &item.of.table);
    wasmtime_extern_delete(&item);
    wasmtime_val_t v = {};
    v.kind = WASMTIME_I64;
    v.of.i64 = static_cast<std::int64_t>(sz);
    return check_single_result(sess, v, row.expect, err_msg);
  }

  if ( row.action == "TABLE_GET" )
  {
    wasmtime_extern_t item = {};
    if ( !wasmtime_instance_export_get(sess.ctx, &sess.instance, row.target.c_str(),
                                       row.target.size(), &item)
         || item.kind != WASMTIME_EXTERN_TABLE )
    {
      err_msg = "table export not found: " + row.target;
      return false;
    }
    const size_t colon = row.args.find(':');
    const uint64_t idx
      = parse_u64_bits(colon == std::string::npos ? row.args : row.args.substr(colon + 1));
    wasmtime_val_t val = {};
    if ( !wasmtime_table_get(sess.ctx, &item.of.table, idx, &val) )
    {
      wasmtime_extern_delete(&item);
      err_msg = "wasmtime_table_get out of bounds";
      return false;
    }
    wasmtime_extern_delete(&item);
    const bool ok = check_single_result(sess, val, row.expect, err_msg);
    wasmtime_val_unroot(&val);
    return ok;
  }

  if ( row.action == "TABLE_CALL" )
  {
    // target format: "env.table:<slot>"
    const size_t colon = row.target.find(':');
    const uint64_t slot = parse_u64_bits(row.target.substr(colon + 1));
    if ( !sess.has_imported_table )
    {
      err_msg = "no imported table in session";
      return false;
    }
    wasmtime_val_t elem = {};
    if ( !wasmtime_table_get(sess.ctx, &sess.imported_table, slot, &elem)
         || elem.kind != WASMTIME_FUNCREF || wasmtime_funcref_is_null(&elem.of.funcref) )
    {
      err_msg = "imported table slot is null or out of bounds: " + std::to_string(slot);
      return false;
    }
    const wasmtime_func_t fn = elem.of.funcref;
    return execute_func_and_check(sess, fn, row.args, row.expect, err_msg);
  }

  if ( row.action == "MEM_WRITE_HEX" )
  {
    uint8_t *data = nullptr;
    size_t size = 0;
    if ( !get_active_memory(sess, &data, &size, err_msg) )
      return false;
    const size_t offset = static_cast<size_t>(parse_u64_bits(row.target));
    const size_t nbytes = row.args.size() / 2;
    if ( offset + nbytes > size )
    {
      err_msg = "MEM_WRITE_HEX out of bounds";
      return false;
    }
    for ( size_t i = 0; i < nbytes; ++i )
    {
      const std::string byte_str = row.args.substr(i * 2, 2);
      data[offset + i] = static_cast<uint8_t>(std::strtoul(byte_str.c_str(), nullptr, 16));
    }
    return true;
  }

  if ( row.action == "MEM_FILL" )
  {
    uint8_t *data = nullptr;
    size_t size = 0;
    if ( !get_active_memory(sess, &data, &size, err_msg) )
      return false;
    const size_t colon = row.target.find(':');
    const size_t offset = static_cast<size_t>(parse_u64_bits(row.target.substr(0, colon)));
    const size_t length = static_cast<size_t>(parse_u64_bits(row.target.substr(colon + 1)));
    const auto byte_val = static_cast<uint8_t>(parse_u32_bits(row.args));
    if ( offset + length > size )
    {
      err_msg = "MEM_FILL out of bounds";
      return false;
    }
    std::memset(data + offset, byte_val, length);
    return true;
  }

  if ( row.action == "MEM_ASSERT_HEX" )
  {
    uint8_t *data = nullptr;
    size_t size = 0;
    if ( !get_active_memory(sess, &data, &size, err_msg) )
      return false;
    const size_t colon = row.target.find(':');
    const size_t offset = static_cast<size_t>(parse_u64_bits(row.target.substr(0, colon)));
    const size_t length = static_cast<size_t>(parse_u64_bits(row.target.substr(colon + 1)));
    if ( offset + length > size || row.args.size() != length * 2 )
    {
      err_msg = "MEM_ASSERT_HEX size/bounds mismatch";
      return false;
    }
    for ( size_t i = 0; i < length; ++i )
    {
      const std::string byte_str = row.args.substr(i * 2, 2);
      const auto exp_b = static_cast<uint8_t>(std::strtoul(byte_str.c_str(), nullptr, 16));
      if ( data[offset + i] != exp_b )
      {
        char buf[128];
        std::snprintf(buf, sizeof(buf),
                      "MEM_ASSERT_HEX mismatch at offset %zu+%zu: got 0x%02x, expected 0x%02x",
                      offset, i, data[offset + i], exp_b);
        err_msg = buf;
        return false;
      }
    }
    return true;
  }

  if ( row.action == "MEM_GROW" )
  {
    const uint64_t delta = parse_u64_bits(row.target);
    uint64_t prev_size = 0;
    wasmtime_error_t *err = nullptr;
    if ( sess.has_imported_memory )
    {
      err = wasmtime_memory_grow(sess.ctx, &sess.imported_memory, delta, &prev_size);
    }
    else if ( sess.has_imported_shared_memory )
    {
      err = wasmtime_sharedmemory_grow(sess.imported_shared_memory, delta, &prev_size);
    }
    else
    {
      wasmtime_extern_t item = {};
      if ( wasmtime_instance_export_get(sess.ctx, &sess.instance, "memory", 6, &item)
           && item.kind == WASMTIME_EXTERN_MEMORY )
      {
        err = wasmtime_memory_grow(sess.ctx, &item.of.memory, delta, &prev_size);
        wasmtime_extern_delete(&item);
      }
      else
      {
        err_msg = "MEM_GROW: no memory found";
        return false;
      }
    }

    if ( row.expect == "FAIL" )
    {
      if ( err == nullptr )
      {
        err_msg = "MEM_GROW expected FAIL, but succeeded";
        return false;
      }
      wasmtime_error_delete(err);
      return true;
    }
    if ( err != nullptr )
    {
      err_msg = "MEM_GROW failed: " + format_error(err);
      return false;
    }
    wasmtime_val_t v = {};
    v.kind = WASMTIME_I64;
    v.of.i64 = static_cast<std::int64_t>(prev_size);
    return check_single_result(sess, v, row.expect, err_msg);
  }

  if ( row.action == "HOST_LOG_CLEAR" )
  {
    sess.host_log.entries.clear();
    return true;
  }

  if ( row.action == "HOST_LOG_ASSERT" )
  {
    std::string joined;
    for ( size_t i = 0; i < sess.host_log.entries.size(); ++i )
    {
      if ( i != 0 )
        joined += ",";
      joined += sess.host_log.entries[i];
    }
    if ( joined != row.expect )
    {
      err_msg = "HOST_LOG_ASSERT mismatch: got '" + joined + "', expected '" + row.expect + "'";
      return false;
    }
    return true;
  }

  err_msg = "unknown action: " + row.action;
  return false;
}

std::vector<CaseRow> parse_cases_tsv(const std::filesystem::path &path)
{
  std::ifstream in(path);
  if ( !in )
  {
    std::fprintf(stderr, "runtime_oracle: cannot open %s\n", path.string().c_str());
    std::exit(2);
  }
  std::vector<CaseRow> rows;
  std::string line;
  size_t line_no = 0;
  while ( std::getline(in, line) )
  {
    ++line_no;
    if ( line.empty() || line[0] == '#' )
      continue;
    const auto cols = split_char(line, '\t');
    if ( cols.size() < 6 )
    {
      std::fprintf(stderr, "runtime_oracle: malformed row at line %zu\n", line_no);
      std::exit(2);
    }
    CaseRow r;
    r.line_no = line_no;
    r.scenario = cols[0];
    r.fixture = cols[1];
    r.action = cols[2];
    r.target = cols[3];
    r.args = cols[4];
    r.expect = cols[5];
    r.notes = (cols.size() >= 7) ? cols[6] : "-";
    rows.push_back(r);
  }
  return rows;
}

int run_oracle(int argc, char **argv)
{
  std::filesystem::path cases_path = "tests/runtime_oracle/cases.tsv";
  std::filesystem::path fixtures_dir = "tests/fixtures";
  std::string filter_scenario;
  std::vector<std::string> extra_args;

  for ( int i = 1; i < argc; ++i )
  {
    const std::string arg = argv[i];
    if ( arg == "--cases" && i + 1 < argc )
      cases_path = argv[++i];
    else if ( arg == "--fixtures" && i + 1 < argc )
      fixtures_dir = argv[++i];
    else if ( arg == "--scenario" && i + 1 < argc )
      filter_scenario = argv[++i];
    else
      extra_args.push_back(arg);
  }

  const auto rows = parse_cases_tsv(cases_path);

  ActiveSession sess;
  sess.engine = create_full_engine();

  std::map<std::string, int> scenario_rows;
  std::map<std::string, int> scenario_fails;
  int total_rows = 0;
  int failed_rows = 0;

  // Special multi-file loop support for verify_link_gc_* when invoked with multiple extra_args
  if ( !filter_scenario.empty() && extra_args.size() > 1
       && (filter_scenario == "verify_link_gc_struct_new"
           || filter_scenario == "verify_link_gc_i31_element"
           || filter_scenario == "verify_link_gc_array_new_element") )
  {
    for ( const auto &farg : extra_args )
    {
      std::vector<std::string> single_arg = { farg };
      for ( const auto &row : rows )
      {
        if ( row.scenario != filter_scenario )
          continue;
        ++total_rows;
        ++scenario_rows[row.scenario];
        std::string err_msg;
        if ( !execute_row(sess, row, fixtures_dir, single_arg, err_msg) )
        {
          ++failed_rows;
          ++scenario_fails[row.scenario];
          std::fprintf(stderr, "FAIL [%s:%zu] %s %s (%s): %s\n", row.scenario.c_str(), row.line_no,
                       row.action.c_str(), row.target.c_str(), farg.c_str(), err_msg.c_str());
        }
      }
    }
  }
  else
  {
    std::string current_scenario;
    for ( const auto &row : rows )
    {
      if ( !filter_scenario.empty() && row.scenario != filter_scenario )
        continue;
      if ( row.scenario != current_scenario )
      {
        sess.reset();
        current_scenario = row.scenario;
      }
      ++total_rows;
      ++scenario_rows[row.scenario];
      std::string err_msg;
      if ( !execute_row(sess, row, fixtures_dir, extra_args, err_msg) )
      {
        ++failed_rows;
        ++scenario_fails[row.scenario];
        std::fprintf(stderr, "FAIL [%s:%zu] %s %s: %s\n", row.scenario.c_str(), row.line_no,
                     row.action.c_str(), row.target.c_str(), err_msg.c_str());
      }
    }
  }

  int passed_scenarios = 0;
  for ( const auto &[sc, cnt] : scenario_rows )
  {
    if ( scenario_fails[sc] == 0 )
      ++passed_scenarios;
  }

  std::printf("WASMHEX_RUNTIME_ORACLE scenarios=%d/%zu rows=%d/%d fail=%d\n", passed_scenarios,
              scenario_rows.size(), total_rows - failed_rows, total_rows, failed_rows);
  return (failed_rows == 0 && !scenario_rows.empty()) ? 0 : 1;
}

} // namespace

int main(int argc, char **argv)
{
  try
  {
    return run_oracle(argc, argv);
  }
  catch ( const std::exception &ex )
  {
    std::fprintf(stderr, "runtime_oracle error: %s\n", ex.what());
    return 1;
  }
}
