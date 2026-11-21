#pragma once
namespace wasmhex
{
// load_wasmhex_hexrays_host(): copy the stock hexx64.dylib, apply the guarded
// patch table, and fail closed with a clear reason on any host outside the
// support table (disassembly keeps working; the F5 filter stays disabled).
bool load_wasmhex_hexrays_host();
}
