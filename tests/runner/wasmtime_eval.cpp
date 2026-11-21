#include "wasmtime_eval.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>
#include <vector>

#if defined(WASMHEX_HAVE_WASMTIME) && WASMHEX_HAVE_WASMTIME
#include <wasmtime.h>
#endif

namespace wasmhex_runner
{

bool wasmtime_eval_i32_1(const std::string &wasm_path, const char *export_name,
                         const std::vector<std::pair<uint32_t, int32_t>> &cases,
                         std::string *out_error)
{
#if defined(WASMHEX_HAVE_WASMTIME) && WASMHEX_HAVE_WASMTIME
  std::ifstream ifs(wasm_path, std::ios::in | std::ios::binary);
  if ( !ifs )
  {
    if ( out_error )
      *out_error = "cannot open " + wasm_path;
    return false;
  }
  std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(ifs)),
                             std::istreambuf_iterator<char>());
  if ( bytes.empty() )
  {
    if ( out_error )
      *out_error = "empty wasm file " + wasm_path;
    return false;
  }

  wasm_config_t *config = wasm_config_new();
  wasm_engine_t *engine = wasm_engine_new_with_config(config);
  if ( engine == nullptr )
  {
    if ( out_error )
      *out_error = "wasm_engine_new_with_config failed";
    return false;
  }
  wasmtime_store_t *store = wasmtime_store_new(engine, nullptr, nullptr);
  wasmtime_context_t *ctx = wasmtime_store_context(store);

  wasmtime_module_t *mod = nullptr;
  wasmtime_error_t *err = wasmtime_module_new(engine, bytes.data(), bytes.size(), &mod);
  if ( err != nullptr )
  {
    wasm_byte_vec_t msg;
    wasmtime_error_message(err, &msg);
    if ( out_error )
      *out_error = std::string("module_new: ") + std::string(msg.data, msg.size);
    wasm_byte_vec_delete(&msg);
    wasmtime_error_delete(err);
    wasmtime_store_delete(store);
    wasm_engine_delete(engine);
    return false;
  }

  wasmtime_instance_t instance{};
  wasm_trap_t *init_trap = nullptr;
  err = wasmtime_instance_new(ctx, mod, nullptr, 0, &instance, &init_trap);
  if ( err != nullptr || init_trap != nullptr )
  {
    if ( err != nullptr )
    {
      wasm_byte_vec_t msg;
      wasmtime_error_message(err, &msg);
      if ( out_error )
        *out_error = std::string("instance_new: ") + std::string(msg.data, msg.size);
      wasm_byte_vec_delete(&msg);
      wasmtime_error_delete(err);
    }
    if ( init_trap != nullptr )
    {
      wasm_message_t msg;
      wasm_trap_message(init_trap, &msg);
      if ( out_error )
        *out_error = std::string("instance_new trap: ") + std::string(msg.data, msg.size);
      wasm_byte_vec_delete(&msg);
      wasm_trap_delete(init_trap);
    }
    wasmtime_module_delete(mod);
    wasmtime_store_delete(store);
    wasm_engine_delete(engine);
    return false;
  }

  wasmtime_extern_t item{};
  if ( !wasmtime_instance_export_get(ctx, &instance, export_name, std::strlen(export_name), &item)
       || item.kind != WASMTIME_EXTERN_FUNC )
  {
    if ( out_error )
      *out_error = std::string("export not found: ") + export_name;
    wasmtime_extern_delete(&item);
    wasmtime_module_delete(mod);
    wasmtime_store_delete(store);
    wasm_engine_delete(engine);
    return false;
  }
  wasmtime_func_t func = item.of.func;

  bool all_ok = true;
  for ( const auto &kv : cases )
  {
    wasmtime_val_t arg{};
    arg.kind = WASMTIME_I32;
    arg.of.i32 = static_cast<int32_t>(kv.first);

    wasmtime_val_t res{};
    wasm_trap_t *trap = nullptr;
    err = wasmtime_func_call(ctx, &func, &arg, 1, &res, 1, &trap);
    if ( err != nullptr || trap != nullptr )
    {
      if ( err != nullptr )
      {
        wasm_byte_vec_t msg;
        wasmtime_error_message(err, &msg);
        if ( out_error )
          *out_error = std::string("call error: ") + std::string(msg.data, msg.size);
        wasm_byte_vec_delete(&msg);
        wasmtime_error_delete(err);
      }
      if ( trap != nullptr )
      {
        wasm_message_t msg;
        wasm_trap_message(trap, &msg);
        if ( out_error )
          *out_error = std::string("call trap: ") + std::string(msg.data, msg.size);
        wasm_byte_vec_delete(&msg);
        wasm_trap_delete(trap);
      }
      all_ok = false;
      break;
    }
    if ( res.kind != WASMTIME_I32 || res.of.i32 != kv.second )
    {
      if ( out_error )
      {
        std::ostringstream oss;
        oss << kv.first << " -> " << res.of.i32 << " != " << kv.second;
        *out_error = oss.str();
      }
      all_ok = false;
      break;
    }
  }

  wasmtime_module_delete(mod);
  wasmtime_store_delete(store);
  wasm_engine_delete(engine);
  return all_ok;
#else
  (void)wasm_path;
  (void)export_name;
  (void)cases;
  if ( out_error )
    *out_error = "wasmdec_runner built without libwasmtime";
  return false;
#endif
}

} // namespace wasmhex_runner
