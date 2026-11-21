#pragma once
// Loader-side border of the wasmhex front end: everything pure
// (reader, module data model, parser decls) lives in core/types.hpp; this
// header keeps only the IDA SDK interfaces and netnode persistence helpers.
#include <loader.hpp>
#include <diskio.hpp>
#include <segment.hpp>
#include <funcs.hpp>
#include <frame.hpp>
#include <name.hpp>
#include <typeinf.hpp>
#include <bytes.hpp>
#include <netnode.hpp>
#include <idaldr.h>

#include <algorithm>
#include <cstring>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "../../core/types.hpp"
#include "../../core/toolchains/go/types.hpp"
#include "../../core/toolchains/go/ir.hpp"
#include "../../core/analysis/mem_drefs.hpp"
#include "../../core/analysis/param_pointers.hpp"
#include "../../core/validation/validate.hpp"
#include "../../backend/idb/metadata_schema/call_metadata.hpp"
#include "../../backend/hexrays/register_mapping/frame.hpp"
#include "../../core/validation/families/atomic_validator.hpp"
#include "../../backend/hexrays/register_mapping/abi.hpp"

namespace wasmhex::loader
{
using wasmhex::get_wasm_argument_register;
using wasmhex::wasm_argument_on_stack;
using wasmhex::wasm_argument_word_count;
using wasmhex::make_wasm_argument_location;
using wasmhex::append_wasm_user_argument;

constexpr ea_t no_memory_code_base = 0x1000;

// Upper bound (exclusive) of every linear address the module can ever
// access: fixed memory -> fixed bytes; a declared maximum -> that maximum
// (growth can never pass it); memory32 without one -> 4 GiB. An unbounded
// memory64 never gets this far — the loader refuses it up front.
inline uint64 linear_memory_upper_bound(const module_t &module)
{
  if ( module.fixed_memory() )
    return module.fixed_memory_bytes;
  if ( module.memory_has_declared_maximum )
  {
    // Saturation guard: the validator admits up to 2^48 pages, and
    // 2^48 * 65536 == 2^64 wraps to 0 in uint64 — saturate so the
    // caller's "no address space above the memory" gate fires instead.
    if ( module.memory_declared_maximum_pages > (UINT64_MAX >> 16) )
      return UINT64_MAX;
    return module.memory_declared_maximum_pages * 65536;
  }
  return uint64(wasmhex_max_growable_memory_pages) * 65536; // 4 GiB cap (memory32)
}

// .text base: above the memory domain whenever memory exists. Returns
// BADADDR when no EA remains above the (aligned) upper bound — the caller
// fails closed with a diagnostic (huge fixed memory64 / saturated bound).
inline ea_t d1_code_base(const module_t &module)
{
  if ( module.memory_count == 0 )
    return no_memory_code_base;
  const uint64 upper = std::max<uint64>(0x100000000ULL, linear_memory_upper_bound(module));
  if ( upper > UINT64_MAX - 0x1000 )
    return BADADDR;
  return ea_t((upper + 0xFFF) & ~uint64(0xFFF));
}

// Prototype text builders (IDA tinfo-backed; qstring in/out — callers on the
// core side convert at the call site) and known import prototypes.
struct known_import_prototype_t
{
  const char *name;
  const char *decl;
};

inline static const known_import_prototype_t standard_import_prototypes[] = {
  { "free", "void __usercall dummy(void *ptr@<r0>);" },
  { "malloc", "void * __usercall dummy@<r0>(unsigned int size@<r0>);" },
  { "memmove",
    "void * __usercall dummy@<r0>(void *dest@<r0>, const void *src@<r1>, unsigned int n@<r2>);" },
  { "memset", "void * __usercall dummy@<r0>(void *s@<r0>, int c@<r1>, unsigned int n@<r2>);" },
  { "memcpy",
    "void * __usercall dummy@<r0>(void *dest@<r0>, const void *src@<r1>, unsigned int n@<r2>);" },
  { "_Znwj",
    "void * __usercall dummy@<r0>(unsigned int size@<r0>);" }, // operator new(unsigned int)
  { "_Znwm",
    "void * __usercall dummy@<r0>(unsigned int size@<r0>);" }, // operator new(unsigned long)
  { "_ZdlPv", "void __usercall dummy(void *ptr@<r0>);" },      // operator delete(void *)
  { "_ZdaPv", "void __usercall dummy(void *ptr@<r0>);" },      // operator delete[](void *)
  { "abort", "void __usercall __noreturn dummy();" },
  { "_abort_js", "void __usercall __noreturn dummy();" },
  { "exit", "void __usercall __noreturn dummy(int status@<r0>);" },
  { "_exit", "void __usercall __noreturn dummy(int status@<r0>);" },
  { "proc_exit", "void __usercall __noreturn dummy(int exit_code@<r0>);" },
  { "__resumeException", "void __usercall __noreturn dummy(void *exception@<r0>);" },
  { "__cxa_throw",
    "void __usercall __noreturn dummy(void *thrown_exception@<r0>, void *tinfo@<r1>, void *dest@<r2>);" },
  { "__cxa_rethrow", "void __usercall __noreturn dummy();" },
  { "__cxa_rethrow_primary_exception", "void __usercall __noreturn dummy(void *exception@<r0>);" },
  { "_emscripten_throw_longjmp", "void __usercall __noreturn dummy();" },
  { "__assert_fail",
    "void __usercall __noreturn dummy(const char *assertion@<r0>, const char *file@<r1>, unsigned int line@<r2>, const char *function@<r3>);" },
  { "__throw_bad_alloc", "void __usercall __noreturn dummy();" },
  { "_ZSt17__throw_bad_allocv", "void __usercall __noreturn dummy();" },
  { "__throw_bad_array_new_length", "void __usercall __noreturn dummy();" },
  { "_ZSt28__throw_bad_array_new_lengthv", "void __usercall __noreturn dummy();" },
  { "_ZNSt3__128__throw_bad_array_new_lengthEv", "void __usercall __noreturn dummy();" },
  { "__cxa_throw_bad_array_new_length", "void __usercall __noreturn dummy();" },
  { "__throw_length_error", "void __usercall __noreturn dummy(const char *msg@<r0>);" },
  { "_ZSt20__throw_length_errorPKc", "void __usercall __noreturn dummy(const char *msg@<r0>);" },
  { "_ZNSt3__220__throw_length_errorEPKc",
    "void __usercall __noreturn dummy(const char *msg@<r0>);" },
  { "__throw_out_of_range", "void __usercall __noreturn dummy(const char *msg@<r0>);" },
  { "_ZSt20__throw_out_of_rangePKc", "void __usercall __noreturn dummy(const char *msg@<r0>);" },
  { "_ZNSt3__220__throw_out_of_rangeEPKc",
    "void __usercall __noreturn dummy(const char *msg@<r0>);" },
  { "__throw_logic_error", "void __usercall __noreturn dummy(const char *msg@<r0>);" },
  { "_ZSt19__throw_logic_errorPKc", "void __usercall __noreturn dummy(const char *msg@<r0>);" },
  { "_ZNSt3__219__throw_logic_errorEPKc",
    "void __usercall __noreturn dummy(const char *msg@<r0>);" },
  { "__throw_runtime_error", "void __usercall __noreturn dummy(const char *msg@<r0>);" },
  { "_ZSt21__throw_runtime_errorPKc", "void __usercall __noreturn dummy(const char *msg@<r0>);" },
  { "_ZNSt3__221__throw_runtime_errorEPKc",
    "void __usercall __noreturn dummy(const char *msg@<r0>);" },
  { "_ZNSt3__224__throw_invalid_argumentEPKc",
    "void __usercall __noreturn dummy(const char *msg@<r0>);" },
  { "_ZSt9terminatev", "void __usercall __noreturn dummy();" },
};

const char *find_standard_import_prototype(const qstring &name, const wasm_type_t &type);

// True when the pre-built Go function IR (blob 'x' GOFR tail) proves
// this defined body is a void Go function (frame proven, zero result-slot
// stores, at least one `return`). entry.cpp displays those as `void f(...)`
// while 'P'/'Q' keep the raw wasm RETUNWIND i32 for call sites.
bool go_proven_void_results(const module_t &module, std::size_t body_index);

// Like go_proven_void_results but for REAL results — true (and
// `types` filled with the wasm result vector, one entry per deduplicated
// result slot, sp_offset-sorted) when the frame evidence recorded result
// stores AND the module-wide caller cross-check confirmed them
// (`results_caller_state == kResultsProven`). Fail-closed: bodies without a
// resolved caller or with a window/read mismatch keep the raw display.
bool go_proven_results(const module_t &module, std::size_t body_index, std::vector<uchar> *types);

// True (and `c_name`/`c_fields`/`c_size` filled) when the
// multi-result proof holds AND a named-signature anchor proves the result
// is exactly one named Go composite of the same byte size — entry.cpp then
// renders the sret pointer as `struct go__X *result` instead of
// `void *result` (the virtual struct is created in the til first).
bool go_named_sret_pointee(const module_t &module, std::size_t body_index,
                           const std::string &function_name, std::string *c_name,
                           std::vector<go_c_field_t> *c_fields = nullptr,
                           std::uint64_t *c_size = nullptr);

bool make_prototype(const wasm_type_t &type, const qstring &name, qstring *decl,
                    const std::vector<qstring> *param_names = nullptr);

// Prototypes with real parameter types recovered from the demangled
// Itanium name (builtins + opaque struct pointers), size-checked against
// the wasm signature. Empty result means "keep the wasm fallback".
std::vector<std::string> split_demangled_parameters(const std::string &text);
std::string mangled_param_type(const std::string &raw, uchar wasm_type);
bool make_mangled_prototype(const std::string &demangled, const std::string &prefix,
                            const wasm_type_t &type, const std::vector<qstring> *param_names,
                            qstring *decl);

// Multi-result ABI helpers live in ida/abi.cpp; import them here.
using wasmhex::multi_result_word_count;
using wasmhex::tuple_type_name;
using wasmhex::make_multi_return_location;
using wasmhex::tuple_layout_matches;
using wasmhex::get_multi_result_type;

bool make_multi_prototype(const wasm_type_t &type, tinfo_t *out, bool force_sret = false,
                          const char *sret_pointee = nullptr,
                          // nullptr = the phantom sret slot binds a
                          // caller argument group — downgrade the name.
                          const char *result_name = "result");

// Netnode persistence (database.cpp). Core containers cross the border by
// const&/pointer; these helpers serialize them into "$ wasmhex" supvals.
bool store_type_vector(netnode &node, uint32 index, uchar tag, const std::vector<uchar> &types);

bool store_type_vector(netnode &node, ea_t ea, uchar tag, const std::vector<uchar> &types);

bool encode_reference_metadata(bytevec *encoded, const std::vector<int64> &heap_types,
                               const std::vector<uchar> &ref_forms);

bool store_reference_metadata(netnode &node, uint32 index, uchar tag,
                              const std::vector<int64> &heap_types,
                              const std::vector<uchar> &ref_forms);

bool store_reference_metadata(netnode &node, ea_t ea, uchar tag,
                              const std::vector<int64> &heap_types,
                              const std::vector<uchar> &ref_forms);

bool store_table_slots(netnode &node, const std::vector<table_slot_t> &slots);

bool store_table_metadata(netnode &node, const module_t &module);

bool build_global_storage(const module_t &module, bytevec *storage);

bool store_global_metadata(netnode &node, uint32 index, const global_t &global, ea_t address);

// Loader entry points (entry.cpp)
int idaapi accept_file(qstring *fileformatname, qstring *processor, linput_t *li, const char *);

wasmhex_bounds_mode_t read_wasmhex_bounds_mode();

qstring read_wasmhex_c_stack_global_override();

void idaapi load_file(linput_t *li, ushort, const char *);
}
