#pragma once

#include "profile.hpp"

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace wasmhex::loader
{

struct module_t;

// pclntab retention
struct go_pclntab_anchor_t
{
  bool present = false;
  std::uint64_t header = 0;      // pcHeader address (F1/F0 magic + shape validated)
  std::uint64_t nfunc = 0;       // pcHeader.nfunc
  std::int64_t funcname_off = 0; // pcHeader-relative offsets (F1 field layout:
  std::int64_t cutab_off = 0;    // nfunc@8, unused@24, funcnameOff@32, cuOff@40)
  std::int64_t pcln_off = 0;
  std::int64_t ftab_base = 0; // header + pcln_off (functab / _func base)
  // Per DEFINED function index (imports excluded): the _func struct address
  // (ftab_base + ftab[i].funcoff, entry mapped by the entryoff-delta rule
  // shared with the name recovery). 0 = unmapped.
  std::vector<std::uint64_t> func_addrs;
};

// Type descriptors (internal/abi.Type, ptrSize=8, 48-byte header)
struct go_type_field_t
{
  std::string name;            // decoded [flags][uvarint len][bytes] at the field Name
  std::uint64_t offset = 0;    // StructField.Offset
  std::uint64_t type_addr = 0; // StructField.Typ (real address; 0 = unreadable)
};

struct go_type_descriptor_t
{
  std::uint64_t addr = 0;        // rtype address (== types + typeOff for roots)
  std::uint8_t kind = 0;         // @23 (abi.Kind: 18 chan .. 25 struct)
  std::uint64_t size = 0;        // @0
  std::uint8_t tflag = 0;        // @20 (1 uncommon, 2 extra-star, 4 named, ...)
  std::int32_t str_off = 0;      // @40 raw NameOff (types-relative)
  std::string name;              // decoded name at types+str_off (raw '*' kept)
  std::uint64_t ptr_to_this = 0; // resolved types+PtrToThis (0 = none)

  // Kind-specific payload (0/empty = not applicable or unreadable):
  std::uint64_t elem_addr = 0;         // Pointer/Slice/Chan/Array/Map(key) @48
  std::uint64_t elem2_addr = 0;        // Array.Slice @56 / Map.elem @56
  std::uint64_t array_len = 0;         // Array.Len @64
  std::uint64_t fields_addr = 0;       // Struct.Fields ptr @56
  std::vector<go_type_field_t> fields; // Struct fields (24B each)
  std::uint16_t in_count = 0;          // Func.InCount @48
  std::uint16_t out_count = 0;         // Func.OutCount @50 (bit15 = variadic)
  bool variadic = false;
  std::vector<std::uint64_t> param_addrs; // Func in/out types (real addresses)
  std::uint16_t method_count = 0;         // UncommonType.Mcount / Interface methods
  std::string pkg_path;                   // UncommonType/Interface PkgPath name

  // True when scan_go_types' eager reference closure added this
  // descriptor (not a walk root). Informational only — the anchor
  // matchers treat closure and lazily-resolved descriptors identically.
  bool eager_closure = false;
};

// ArgInfo (funcdata[5], abi.TraceArgs encoding)
struct go_arginfo_slot_t
{
  std::uint32_t offset = 0; // byte from the argument-area base (SP+8+off)
  std::uint32_t size = 0;
  std::uint8_t depth = 0; // aggregate nesting depth of the pair (0 = top level)
};

struct go_arginfo_t
{
  bool present = false;
  std::int32_t args_bytes = 0;          // _func.args: total in/out argument frame bytes
  std::vector<go_arginfo_slot_t> slots; // decoded {offset,size} pairs
  // Top-level aggregate groups. The TraceArgs byte stream marks every
  // aggregate with StartAgg/EndAgg operators (0xfe/0xfd); the {offset,size,
  // depth} slots alone cannot delimit consecutive top-level aggregates (a
  // 5-slot depth-1 run may be one struct or a string followed by a slice).
  // This parallel vector has one flag per slot: 1 = the slot opens a
  // top-level aggregate parameter (the run of consecutive depth>=1 slots
  // from a flagged slot up to the next flag or the first depth-0 slot is
  // one aggregate argument). Depth-0 slots are never flagged: a scalar
  // parameter is a single unflagged slot by construction.
  std::vector<std::uint8_t> group_starts;
};

// The layer result (module_t::go_types)

struct go_itab_method_t
{
  std::string name;                    // "Error" (Imethod name, decoded)
  std::uint64_t func_type_addr = 0;    // Imethod typ (real addr; 0 = gap-cut)
  std::uint32_t fn_index = UINT32_MAX; // module fn index via table0 slot
};

struct go_itab_t
{
  std::uint64_t addr = 0;
  std::uint64_t inter_addr = 0;
  std::uint64_t type_addr = 0;
  std::string inter_name; // display name ("error" — extra star stripped)
  std::string type_name;  // concrete type ("*errors.errorString")
  std::vector<go_itab_method_t> methods;
};

// One virtual til struct field (name + C type string + byte extent).
struct go_c_field_t
{
  std::string name;
  std::string c_type;
  std::uint64_t offset = 0;
  std::uint64_t size = 0;
};

struct go_c_struct_t
{
  std::string c_name;
  std::vector<go_c_field_t> fields;
  std::uint64_t size = 0;
};

enum class go_layout_source_t : std::uint8_t
{
  kNone = 0,
  kDescriptor = 1, // tier (a)
  kArgInfo = 2,    // tier (b)
  kEvidence = 3,   // tier (c)
};

struct go_layout_param_t
{
  bool is_group = false;            // false: scalar word; true: aggregate group
  std::uint32_t offset = 0;         // argument-window byte offset
  std::uint32_t bytes = 0;          // scalar: slot bytes; group: aggregate extent
  std::uint8_t type = 0;            // scalar: wasm value type; group: 0
  std::string c_name;               // "" = canonical extent name; else "go__X"
  std::vector<go_c_field_t> fields; // named struct groups (til registration)
};

struct go_func_layout_t
{
  go_layout_source_t source = go_layout_source_t::kNone;
  std::vector<go_layout_param_t> params;
  // Tier (a) only: named results at their incoming-window offsets (the
  // _func.args gap tiling proved in go_shape_matches; tier-(b)/(c) records
  // leave this empty — result display stays on the gated GOFR path).
  std::vector<go_layout_param_t> results;
  bool variadic = false;
  std::string anchor; // provenance note ("" = none)
};

// Census (loader summary line + unit-test pin).
struct go_layout_census_t
{
  std::size_t descriptor = 0;      // tier (a)
  std::size_t arginfo = 0;         // tier (b)
  std::size_t evidence = 0;        // tier (c)
  std::size_t skip_no_arginfo = 0; // no funcdata[5] / slots / group flags
  std::size_t skip_shape = 0;      // impossible encoding or broken tiling
  std::size_t total = 0;           // defined bodies considered
};

struct go_type_info_t
{
  bool present = false;
  int layout_version = 0; // 26 or 27 (0 = none matched)
  std::uint64_t moduledata = 0;
  std::uint64_t types = 0;
  std::uint64_t gofunc = 0;
  // 1.26 roots
  std::uint64_t typelinks_ptr = 0;
  std::uint32_t typelinks_len = 0;
  std::uint64_t itablinks_ptr = 0;
  std::uint32_t itablinks_len = 0;
  // 1.27 region facts
  std::uint64_t typedesclen = 0;
  std::uint64_t etypes = 0;
  std::uint64_t itaboffset = 0;
  std::uint64_t itabsize = 0;

  go_pclntab_anchor_t anchor;
  std::vector<go_type_descriptor_t> descriptors;
  std::vector<go_arginfo_t> arginfo; // per DEFINED function index

  // Single-source layouts, per DEFINED function index (empty when the
  // layer is absent). Serialized as the blob-'t' v2 tail.
  std::vector<go_func_layout_t> layouts;

  // The build census (loader summary line + test pin; not serialized).
  go_layout_census_t layout_census;

  std::size_t blob_descriptor_cap = 0;

  // Itab layer, re-derived at load time; NOT serialized in blob 't'.
  std::vector<go_itab_t> itabs;
  bool itabs_scanned = false;

  // Stable-prefix diagnostic when a candidate existed but no layout matched
  // (or the profile cross-check rejected it): "go moduledata layout mismatch:"
  std::string mismatch_reason;

  const go_type_descriptor_t *descriptor_by_addr(std::uint64_t addr) const;
};

// Profile cross-check: the layout family the resolved profile expects.
// go1_26_series <-> 26, go1_27_series <-> 27; every other profile (including
// unknown) has no supported layout this slice.
int go_expected_moduledata_version(go_profile_t profile);

// Scan the module data segments for the moduledata anchored on the retained
// pclntab header, score both layouts, resolve the descriptor roots and the
// per-function ArgInfo. Idempotent; leaves the layer empty (with a mismatch
// reason when a candidate existed) on any failed validation.
void scan_go_types(module_t &module);

// Named-signature resolution for one defined body.
struct go_sig_param_t
{
  std::string display; // Go-style rendering ("[]js.Value", "*main.Ball")
  std::string c_name;  // C struct name for named composites ("go__string"),
                       // "" for scalars / unrepresentable layouts
  std::string name;    // parameter name (known-API curation; "" = aN)
  std::uint64_t size = 0;
};

struct go_named_sig_t
{
  std::uint8_t source = 0;            // 1 itab, 2 known-API, 3 FuncOf callback, 4 descriptor
  std::string label;                  // "error.Error" | "syscall/js.Value.Get" | ...
  std::string anchor;                 // provenance note for the display comment
  std::string receiver;               // itab/descriptor receiver display name ("" = none)
  std::vector<go_sig_param_t> params; // includes the receiver word(s)
  std::vector<go_sig_param_t> results;
  bool variadic = false;
  bool complete = true;  // false = partial signature "(...)" (gap-cut mtyp)
  std::string signature; // rendered display signature
};

bool go_resolve_named_signature(module_t &module, std::size_t body_index,
                                const std::string &function_name, go_named_sig_t *out);
bool go_type_display_name(module_t &module, std::uint64_t type_addr, std::string *out);
bool go_c_struct_for_param(module_t &module, const go_sig_param_t &param, go_c_struct_t *out);

// The display name this body carries (name section first, then pclntab),
// unsanitized — callers sanitize per their naming policy.
std::string go_body_display_name(const module_t &module, std::size_t body_index);

// True when `fn_index` resolves to a known-API
// syscall/js method carrying a js.Value receiver (Value.Call / Set / Get /
// Invoke / New / ...). Defined bodies match on their display name; imports
// keep host spellings that never match the table (fail-closed). Pure —
// usable from the loader's global-renaming evidence walk.
bool go_known_api_jsvalue_callee(const module_t &module, std::uint32_t fn_index);

void go_build_func_layouts(module_t &module,
                           const std::function<std::string(std::size_t)> &body_name,
                           go_layout_census_t *census = nullptr);

// Typed receivers: an INCOMPLETE method signature (anchor-4 gap-cut)
// still proves its receiver — the leading ArgInfo slots tiled the receiver
// words before the partial signature rendered. This resolves the receiver
// display name to its C parameter: a pointer receiver is ONE 8-byte word
// (Go pointers are 8 bytes on wasm), a value receiver is the struct by
// value at the descriptor's size. Fails closed when the descriptor is not
// in the layer or the struct cannot be built.
bool go_receiver_c_param(module_t &module, const go_named_sig_t &sig, bool *is_pointer,
                         go_sig_param_t *out);

bool go_string_literal_text(const std::uint8_t *bytes, std::size_t len, std::string *out);
bool go_boxed_scalar_text(std::uint8_t kind, const std::uint8_t *data, std::string *out);
bool go_member_covering(const go_type_info_t &layer, const go_type_descriptor_t &descriptor,
                        std::uint64_t offset, std::uint64_t size, std::string *name);

struct go_xf_result_info_t
{
  std::uint32_t min_offset = 0;          // lowest proven result slot offset
  std::uint32_t extent = 0;              // one past the highest result byte
  std::string c_name;                    // named composite C name ("go__string")
  std::vector<unsigned char> slot_types; // per-slot display types (the '5' vector)
};

bool go_xf_single_result(const module_t &module, std::size_t body_index,
                         const std::string &function_name, go_xf_result_info_t *out);
// Pure display helpers over a DECODED type layer (blob 't'). No
// module_t and no lazy resolution — every lookup stays inside the descriptor
// set the caller already decoded, so the decompiler side (which only has the
// netnode blob) renders exactly the same names the loader would. All of
// these are fail-closed: any miss (unknown address, gap-cut field, depth
// overflow) returns "" / false.

// Render one type as its Go display string ("map[string]interface {}",
// "[]*main.Ball", "main.Pt", ...). Bounded depth; "" when any hop of the
// reference chain is missing from the layer.
std::string go_display_type_name(const go_type_info_t &layer, std::uint64_t type_addr,
                                 int depth = 0);

// The C-presentation name for one descriptor ("go__string", "go__slice",
// "go__iface", "go__js__Value" for the known js.Value spelling, and
// "go__pkg_Type" for other named structs). "" for scalars and structural
// types a single C word cannot represent.
std::string go_descriptor_c_name(const go_type_descriptor_t &descriptor);

// The Go display name of one decoded descriptor ("string", "js.Value",
// "main.Pt") — the redundant leading '*' of tflagExtraStar stripped.
std::string go_descriptor_type_name(const go_type_descriptor_t &descriptor);

// Build the virtual til layout for a named struct descriptor (the blob-side
// mirror of go_c_struct_for_param's descriptor walk: fields sorted by
// offset, widths from the field type size with the offset-delta fallback,
// scalar C types by kind/size, nested named structs as "struct go__X").
// False for non-structs, unnamed structs and any layout that does not close
// exactly at the descriptor size.
bool go_descriptor_struct_fields(const go_type_info_t &layer,
                                 const go_type_descriptor_t &descriptor,
                                 std::vector<go_c_field_t> *fields, std::uint64_t *size);

// Resolve one more descriptor on demand (lazy reference following — e.g. a
// struct type that lives past the walk region and is only reachable through
// a *T pointer's Elem). Returns nullptr when the address does not decode.
const go_type_descriptor_t *resolve_go_type_addr(module_t &module, std::uint64_t addr);

// Scan the itab layer. Idempotent, fail-closed: an empty list when
// the type layer is absent; a 1.27 walk that does not consume the region
// exactly clears the list. Individual itabs straddling relocation gaps are
// skipped.
const std::vector<go_itab_t> &go_itab_layer(module_t &module);

constexpr std::uint8_t go_types_blob_tag = 't';
constexpr std::uint32_t go_types_blob_version = 2;
constexpr std::uint32_t go_types_blob_v1 = 1;
void encode_go_type_info_blob(const go_type_info_t &info, std::vector<std::uint32_t> *out);
bool decode_go_type_info_blob(const std::uint32_t *words, std::size_t word_count,
                              go_type_info_t *out);

} // namespace wasmhex::loader
