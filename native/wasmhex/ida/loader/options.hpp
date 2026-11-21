#pragma once

#include <pro.h>
#include <netnode.hpp>

namespace wasmhex::options
{
// Diagnostic gate — true when `category` appears in WASMHEX_DEBUG.
// Thread-safe by C++11 statics; cheap after the first call.
bool debug(const char *category);

// Raw one-shot environment read for the loader entry point. Everything
// that runs after load must read the netnode (read_wasmhex_option) or a
// debug gate instead.
bool env_string(const char *name, qstring *out_value);
} // namespace wasmhex::options

namespace wasmhex::loader
{

// Reads option `name`: env override -> netnode `tag` at index 0 -> fallback.
// The first non-default value seen is written back to the netnode.
// `out_value` receives the raw text ("1", "explicit", a global name, ...).
void read_wasmhex_option(netnode &wasm_node, uchar tag, const char *env_name, const char *fallback,
                         qstring *out_value);

// Typed helpers over the raw option.
bool read_wasmhex_option_flag(netnode &wasm_node, uchar tag, const char *env_name, bool fallback);

} // namespace wasmhex::loader
