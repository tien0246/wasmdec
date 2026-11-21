// Go type data layer — implementation. See types.hpp.
#include "types.hpp"

#include "../../binary/instruction_stream.hpp"
#include "../../types.hpp"
#include "ir.hpp"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <set>

namespace wasmhex::loader
{

namespace
{

// Build marker verified in both deployed dylibs (procs + loaders) via
// strings before each test run (pattern from ir.cpp).
extern "C" __attribute__((used)) const char kWasmhexG7TypesBuildMarker[]
  = "WASMHEX_BUILD_MARKER_G7_TYPE_DATA_LAYER";

// 3.4 build marker: pointer receivers keep their `*` in the display (the
// `__T_` name mangling is the proof source). Same strings-on-dylib check.
extern "C" __attribute__((used)) const char kWasmhexM0bPtrReceiverMarker[]
  = "WASMHEX_BUILD_MARKER_M0B_PTR_RECEIVER_DISPLAY";

// Hole-tolerant linear-memory reader over the active data segments (mirrors
// the instantiated memory: bytes no active segment initializes read as 0).
struct go_span_t
{
  int64 dest;
  const uchar *data;
  size_t size;
};

const uchar *go_mem_read(const std::vector<go_span_t> &spans, int64 addr, size_t bytes)
{
  size_t lo = 0, hi = spans.size();
  while ( lo < hi )
  {
    const size_t mid = (lo + hi) / 2;
    if ( spans[mid].dest <= addr )
      lo = mid + 1;
    else
      hi = mid;
  }
  if ( lo == 0 )
    return nullptr;
  const go_span_t &span = spans[lo - 1];
  const int64 offset = addr - span.dest;
  if ( offset < 0 || offset + int64(bytes) > int64(span.size) )
    return nullptr;
  return span.data + size_t(offset);
}

uint64_t go_read_uint(const std::vector<go_span_t> &spans, int64 addr, size_t width)
{
  uint64_t value = 0;
  for ( size_t i = 0; i < width; ++i )
  {
    const uchar *ptr = go_mem_read(spans, addr + int64(i), 1);
    value |= uint64_t(ptr != nullptr ? *ptr : 0) << (i * 8);
  }
  return value;
}

bool go_read_byte(const std::vector<go_span_t> &spans, int64 addr, uint8_t *out)
{
  const uchar *ptr = go_mem_read(spans, addr, 1);
  if ( ptr == nullptr )
    return false;
  *out = *ptr;
  return true;
}

// Decode a Go reflect name: [flags u8][uvarint len][bytes]. The flags byte
// may sit in a relocation gap (the zero-filled value is what the
// instantiated memory holds); the length varint and the name bytes
// themselves must be covered. Fails on a truncated varint, an overlong
// name or non-printable bytes.
bool go_read_name(const std::vector<go_span_t> &spans, int64 addr, std::string *out,
                  uint8_t *flags_out = nullptr)
{
  out->clear();
  if ( addr <= 0 )
    return false;
  const uint8_t flags = uint8_t(go_read_uint(spans, addr, 1));
  uint64_t len = 0;
  size_t shift = 0;
  int64 cursor = addr + 1;
  for ( int i = 0; i < 10; ++i )
  {
    uint8_t byte = 0;
    if ( !go_read_byte(spans, cursor + i, &byte) )
      return false;
    len |= uint64_t(byte & 0x7f) << shift;
    shift += 7;
    if ( (byte & 0x80) == 0 )
    {
      cursor += i + 1;
      break;
    }
    if ( i == 9 )
      return false;
  }
  if ( len == 0 || len > 512 )
    return false;
  for ( uint64_t i = 0; i < len; ++i )
  {
    uint8_t byte = 0;
    if ( !go_read_byte(spans, cursor + int64(i), &byte) || byte < 0x20 || byte > 0x7E )
      return false;
    out->push_back(char(byte));
  }
  if ( flags_out != nullptr )
    *flags_out = flags;
  return true;
}

// moduledata layouts (ptrSize = 8), table-encoded. Offsets verified against
// go1.26.5 and go1.27.3 runtime/symtab.go + the wasm corpora.
struct go_md_layout_t
{
  int version;
  uint16 etypes; // 1.26: etypes@304 / 1.27: typedesclen@304 (et_a)
  uint16 et_b;   // 1.26: rodata@312 / 1.27: etypes@312
  uint16 et_c;   // 1.26: gofunc@320 / 1.27: itaboffset@320
  uint16 et_d;   // 1.26: epclntab@328 / 1.27: itabsize@328
  uint16 et_e;   // 1.26: unused / 1.27: rodata@336
  uint16 gofunc; // 1.26: 320 / 1.27: 344
  uint16 textsectmap_len;
  uint16 typelinks_ptr;
  uint16 typelinks_len;
  uint16 itablinks_ptr;
  uint16 itablinks_len;
};

constexpr go_md_layout_t kGoMdLayout126 = {
  26, 304, 312, 320, 328, 0, 320, 344, 360, 368, 384, 392,
};
constexpr go_md_layout_t kGoMdLayout127 = {
  27, 304, 312, 320, 328, 336, 344, 368, 0, 0, 0, 0,
};

constexpr uint16 kGoMdTypes = 296; // both layouts

constexpr uint8 kGoTFlagUncommon = 0x1; // bit0: UncommonType follows the kind data

// abi.Kind bounds (Bool=1 .. UnsafePointer=26; generics kinds stay 1.27-only
// and are not walked).
constexpr uint8 kGoKindMin = 1;
constexpr uint8 kGoKindMax = 26;
constexpr uint8 kGoKindBool = 1;     // boxed static value kinds
constexpr uint8 kGoKindFloat32 = 13; // (see go_boxed_scalar_text)
constexpr uint8 kGoKindFloat64 = 14;
constexpr uint8 kGoKindArray = 17;
constexpr uint8 kGoKindChan = 18;
constexpr uint8 kGoKindFunc = 19;
constexpr uint8 kGoKindInterface = 20;
constexpr uint8 kGoKindMap = 21;
constexpr uint8 kGoKindPointer = 22;
constexpr uint8 kGoKindSlice = 23;
constexpr uint8 kGoKindStruct = 25;

// Size of the kind-specific fixed header (aligned to 8). Matches Go 1.27
// internal/abi DescriptorSize() so the sequential walk steps exactly like
// runtime.moduleTypelinks. The 1.27 MapType (Key/Elem/Group/Hasher/.../
// Flags) is 136 bytes; Func grows by the in/out *Type array, Interface by
// the inline Imethod array, after the optional UncommonType.
int go_descriptor_fixed_size(const std::vector<go_span_t> &spans, int64 t)
{
  const uint8_t kind = uint8_t(go_read_uint(spans, t + 23, 1));
  switch ( kind )
  {
    case kGoKindArray: return 72;     // Elem, Slice, Len
    case kGoKindChan: return 64;      // Elem, Dir
    case kGoKindFunc: return 56;      // InCount, OutCount (+pad)
    case kGoKindInterface: return 80; // PkgPath, Methods slice
    case kGoKindMap: return 136;      // 1.27 swiss-map layout
    case kGoKindPointer:
    case kGoKindSlice: return 56;  // Elem
    case kGoKindStruct: return 80; // PkgPath, Fields slice
    default: return 48;
  }
}

// Full contiguous descriptor size (internal/abi.Type.DescriptorSize):
// fixed header + optional UncommonType(16) + variable part + methods(16 ea).
int go_descriptor_size(const std::vector<go_span_t> &spans, int64 t)
{
  const uint8_t kind = uint8_t(go_read_uint(spans, t + 23, 1));
  const uint8_t tflag = uint8_t(go_read_uint(spans, t + 20, 1));
  int size = go_descriptor_fixed_size(spans, t);
  int mcount = 0;
  if ( tflag & kGoTFlagUncommon )
  {
    const int64 ucom = t + go_descriptor_fixed_size(spans, t);
    if ( uint64_t(ucom + 8) < uint64_t(t) ) // overflow guard
      return 0;
    size += 16;
    mcount = int(go_read_uint(spans, ucom + 4, 2));
    if ( mcount < 0 || mcount > 4096 )
      return 0;
  }
  switch ( kind )
  {
    case kGoKindFunc:
    {
      const uint16_t in = uint16_t(go_read_uint(spans, t + 48, 2));
      const uint16_t out = uint16_t(go_read_uint(spans, t + 50, 2)) & 0x7fff;
      size += int(in + out) * 8;
      break;
    }
    case kGoKindInterface:
      size += int(go_read_uint(spans, t + 64, 8)) * 8; // Methods.len Imethods
      break;
    default: break;
  }
  if ( size < 48 || size > (1 << 20) )
    return 0;
  size += mcount * 16;
  return size;
}

bool go_kind_valid(uint8_t kind)
{
  return kind >= kGoKindMin && kind <= kGoKindMax;
}

// Decode one rtype at `addr` into `out` (types-relative name resolution).
bool go_decode_descriptor(const std::vector<go_span_t> &spans, int64 addr, int64 types_base,
                          go_type_descriptor_t *out)
{
  out->addr = uint64(addr);
  uint8_t kind = 0;
  if ( !go_read_byte(spans, addr + 23, &kind) || !go_kind_valid(kind) )
    return false;
  out->kind = kind;
  out->size = go_read_uint(spans, addr, 8);
  if ( out->size > (1ull << 40) )
    return false;
  go_read_byte(spans, addr + 20, &out->tflag);
  out->str_off = int32_t(go_read_uint(spans, addr + 40, 4));
  go_read_name(spans, types_base + out->str_off, &out->name);
  const int32_t ptr_off = int32_t(go_read_uint(spans, addr + 44, 4));
  out->ptr_to_this = ptr_off != 0 ? uint64(types_base + ptr_off) : 0;

  switch ( kind )
  {
    case kGoKindPointer:
    case kGoKindSlice:
    case kGoKindChan: out->elem_addr = go_read_uint(spans, addr + 48, 8); break;
    case kGoKindArray:
      out->elem_addr = go_read_uint(spans, addr + 48, 8);
      out->elem2_addr = go_read_uint(spans, addr + 56, 8);
      out->array_len = go_read_uint(spans, addr + 64, 8);
      break;
    case kGoKindMap:
      out->elem_addr = go_read_uint(spans, addr + 48, 8);  // Key
      out->elem2_addr = go_read_uint(spans, addr + 56, 8); // Elem
      break;
    case kGoKindStruct:
    {
      const int32_t pkg = int32_t(go_read_uint(spans, addr + 48, 4));
      go_read_name(spans, types_base + pkg, &out->pkg_path);
      out->fields_addr = go_read_uint(spans, addr + 56, 8);
      const uint64_t count = go_read_uint(spans, addr + 64, 8);
      if ( count > 4096 )
        return false;
      out->fields.reserve(size_t(count));
      for ( uint64_t i = 0; i < count; ++i )
      {
        const int64 field = int64(out->fields_addr) + int64(i) * 24;
        go_type_field_t field_info;
        const uint64_t name_ptr = go_read_uint(spans, field, 8);
        go_read_name(spans, int64(name_ptr), &field_info.name);
        field_info.type_addr = go_read_uint(spans, field + 8, 8);
        field_info.offset = go_read_uint(spans, field + 16, 8);
        out->fields.push_back(std::move(field_info));
      }
      break;
    }
    case kGoKindFunc:
    {
      out->in_count = uint16_t(go_read_uint(spans, addr + 48, 2));
      const uint16_t raw_out = uint16_t(go_read_uint(spans, addr + 50, 2));
      out->out_count = raw_out & 0x7fff;
      out->variadic = (raw_out & 0x8000) != 0;
      if ( out->in_count > 256 || out->out_count > 256 )
        return false;
      const int64 args_base = addr + 56 + ((out->tflag & kGoTFlagUncommon) ? 16 : 0);
      const size_t total = size_t(out->in_count) + size_t(out->out_count);
      out->param_addrs.reserve(total);
      for ( size_t i = 0; i < total; ++i )
        out->param_addrs.push_back(go_read_uint(spans, args_base + int64(i) * 8, 8));
      break;
    }
    case kGoKindInterface:
    {
      const int32_t pkg = int32_t(go_read_uint(spans, addr + 48, 4));
      go_read_name(spans, types_base + pkg, &out->pkg_path);
      out->method_count = uint16_t(go_read_uint(spans, addr + 64, 8));
      break;
    }
    default: break;
  }

  if ( (out->tflag & kGoTFlagUncommon) != 0 && kind != kGoKindFunc )
  {
    const int64 ucom = addr + go_descriptor_fixed_size(spans, addr);
    out->method_count = uint16_t(go_read_uint(spans, ucom + 4, 2));
    if ( out->pkg_path.empty() )
    {
      const int32_t pkg = int32_t(go_read_uint(spans, ucom, 4));
      go_read_name(spans, types_base + pkg, &out->pkg_path);
    }
  }
  return true;
}

// ArgInfo decode (abi.TraceArgs byte stream; FUNCDATA_ArgInfo = 5)
constexpr uint8 kGoTraceArgsEndSeq = 0xff;
constexpr uint8 kGoTraceArgsStartAgg = 0xfe;
constexpr uint8 kGoTraceArgsEndAgg = 0xfd;
constexpr uint8 kGoTraceArgsDotdotdot = 0xfc;
constexpr uint8 kGoTraceArgsOffsetTooLarge = 0xfb;
constexpr uint8 kGoTraceArgsSpecial = 0xf0;
constexpr size_t kGoTraceArgsMaxLen = (5 * 3 + 2) * 10 + 1;

bool go_decode_arginfo(const std::vector<go_span_t> &spans, int64 addr, int32 args_bytes,
                       go_arginfo_t *out)
{
  out->present = false;
  out->args_bytes = args_bytes;
  out->slots.clear();
  out->group_starts.clear();
  uint32_t depth = 0;
  // A StartAgg at depth 0 opens a top-level aggregate parameter; the
  // flag lands on the first decoded slot inside it (that slot may itself sit
  // at depth >= 2 when the aggregate's first field is nested, e.g.
  // struct { string; int }).
  bool pending_group = false;
  for ( size_t i = 0; i < kGoTraceArgsMaxLen; ++i )
  {
    uint8_t op = 0;
    if ( !go_read_byte(spans, addr + int64(i), &op) )
      return false;
    if ( op == kGoTraceArgsEndSeq )
    {
      out->present = true;
      out->group_starts.resize(out->slots.size(), 0);
      return true;
    }
    if ( op == kGoTraceArgsStartAgg )
    {
      if ( depth == 0 )
        pending_group = true;
      ++depth;
      continue;
    }
    if ( op == kGoTraceArgsEndAgg )
    {
      if ( depth == 0 )
        return false;
      --depth;
      continue;
    }
    if ( op == kGoTraceArgsDotdotdot || op == kGoTraceArgsOffsetTooLarge )
      continue;
    if ( op >= kGoTraceArgsSpecial )
      return false; // unknown operator: fail closed
    uint8_t size = 0;
    if ( !go_read_byte(spans, addr + int64(i) + 1, &size) )
      return false;
    if ( depth > 255 )
      return false;
    go_arginfo_slot_t slot;
    slot.offset = op;
    slot.size = size;
    slot.depth = uint8(depth);
    out->slots.push_back(slot);
    out->group_starts.push_back(pending_group ? 1 : 0);
    if ( pending_group )
      pending_group = false;
    ++i;
  }
  return false; // no END within the ABI cap
}

// moduledata candidate scoring
struct go_md_candidate_t
{
  int64 addr = 0;
  bool matched_126 = false;
  bool matched_127 = false;
};

bool go_md_generic_checks(const std::vector<go_span_t> &spans, int64 md,
                          const go_pclntab_anchor_t &anchor)
{
  const uint64_t fnt_ptr = go_read_uint(spans, md + 8, 8);
  if ( fnt_ptr != uint64(int64(anchor.header) + anchor.funcname_off) )
    return false;
  const uint64_t cut_ptr = go_read_uint(spans, md + 32, 8);
  if ( cut_ptr != uint64(int64(anchor.header) + anchor.cutab_off) )
    return false;
  const uint64_t ftab_ptr = go_read_uint(spans, md + 128, 8);
  if ( ftab_ptr != uint64(anchor.ftab_base) )
    return false;
  const uint64_t ftab_len = go_read_uint(spans, md + 136, 8);
  return ftab_len == anchor.nfunc + 1;
}

bool go_md_score_126(const std::vector<go_span_t> &spans, int64 md)
{
  // textsectmap.len == 1 (single text section on wasm)
  if ( go_read_uint(spans, md + kGoMdLayout126.textsectmap_len, 8) != 1 )
    return false;
  const int64 tl_ptr = int64(go_read_uint(spans, md + kGoMdLayout126.typelinks_ptr, 8));
  const uint64_t tl_len = go_read_uint(spans, md + kGoMdLayout126.typelinks_len, 8);
  if ( tl_ptr <= 0 || tl_len == 0 || tl_len > 65536 )
    return false;
  const int64 types = int64(go_read_uint(spans, md + kGoMdTypes, 8));
  if ( types <= 0 )
    return false;
  // "valid typelinks": every offset must decode to a plausible kind byte
  // (small tolerance for relocation-gap kind bytes, which read as 0).
  size_t valid = 0;
  for ( uint64_t i = 0; i < tl_len; ++i )
  {
    const int32_t off = int32_t(go_read_uint(spans, tl_ptr + int64(i) * 4, 4));
    const uint8_t kind = uint8_t(go_read_uint(spans, types + off + 23, 1));
    if ( go_kind_valid(kind) )
      ++valid;
  }
  return valid * 10 >= tl_len * 9 && valid >= 8;
}

bool go_md_score_127(const std::vector<go_span_t> &spans, int64 md)
{
  const int64 types = int64(go_read_uint(spans, md + kGoMdTypes, 8));
  const int64 typedesclen = int64(go_read_uint(spans, md + kGoMdLayout127.etypes, 8));
  const int64 etypes = int64(go_read_uint(spans, md + kGoMdLayout127.et_b, 8));
  const int64 itaboffset = int64(go_read_uint(spans, md + kGoMdLayout127.et_c, 8));
  const int64 itabsize = int64(go_read_uint(spans, md + kGoMdLayout127.et_d, 8));
  const int64 gofunc = int64(go_read_uint(spans, md + kGoMdLayout127.gofunc, 8));
  if ( types <= 0 || typedesclen <= 0 || typedesclen > (1 << 24) )
    return false;
  // Ordering chain: the types blob is
  //   [types, types+typedesclen) decls | [types+itaboffset, +itabsize) itabs
  // with etypes ending the region exactly.
  if ( types + typedesclen > types + itaboffset )
    return false;
  if ( itaboffset >= gofunc )
    return false;
  if ( types + itaboffset + itabsize != etypes )
    return false;
  // textsectmap.len == 1
  return go_read_uint(spans, md + kGoMdLayout127.textsectmap_len, 8) == 1;
}

void build_go_spans(const module_t &module, std::vector<go_span_t> *spans)
{
  for ( size_t i = 0; i < module.data_segment_offsets.size(); ++i )
  {
    if ( module.data_segment_offsets[i] < 0 || module.data_segment_sizes[i] == 0 )
      continue;
    spans->push_back({ module.data_segment_offsets[i], module.data_segment_data[i].data(),
                       size_t(module.data_segment_sizes[i]) });
  }
  std::sort(spans->begin(), spans->end(),
            [](const go_span_t &left, const go_span_t &right) { return left.dest < right.dest; });
}

// Walk the 1.27 decl region [types+8, types+typedesclen) like
// runtime.moduleTypelinks: step by DescriptorSize over valid kind bytes,
// skip 8 bytes over relocation-gap slots (their zero-filled kind bytes are
// invalid). The walk must consume the region exactly.
bool go_walk_types_127(const std::vector<go_span_t> &spans, int64 types, int64 typedesclen,
                       size_t *count)
{
  const int64 end = types + typedesclen;
  int64 t = types + 8;
  size_t descriptors = 0;
  while ( t < end )
  {
    t = (t + 7) & ~int64(7);
    const uint8_t kind = uint8_t(go_read_uint(spans, t + 23, 1));
    if ( !go_kind_valid(kind) )
    {
      t += 8;
      continue;
    }
    const int step = go_descriptor_size(spans, t);
    if ( step < 48 )
      return false;
    ++descriptors;
    t += step;
    if ( descriptors > (1 << 16) )
      return false;
  }
  if ( t != end )
    return false;
  *count = descriptors;
  return true;
}

} // namespace

const go_type_descriptor_t *go_type_info_t::descriptor_by_addr(uint64 addr) const
{
  for ( const go_type_descriptor_t &descriptor : descriptors )
    if ( descriptor.addr == addr )
      return &descriptor;
  return nullptr;
}

int go_expected_moduledata_version(go_profile_t profile)
{
  switch ( profile )
  {
    case go_profile_t::go1_26_series: return 26;
    case go_profile_t::go1_27_series: return 27;
    default: return 0;
  }
}

void scan_go_types(module_t &module)
{
  go_type_info_t fresh;
  fresh.anchor = module.go_types.anchor; // retention survives the rescan
  const go_pclntab_anchor_t &anchor = fresh.anchor;

  std::vector<go_span_t> spans;
  if ( !anchor.present )
  {
    module.go_types = std::move(fresh);
    return;
  }

  build_go_spans(module, &spans);

  // Anchor scan: moduledata starts 8-aligned with pcHeader as word 0. The
  // pointer bytes can straddle relocation gaps (they read as zero, exactly
  // like the instantiated memory), so match span-interior occurrences of the
  // pointer's lowest NON-ZERO byte and verify the whole word bytewise.
  const uint64_t pch = anchor.header;
  int anchor_byte = -1;
  for ( int k = 0; k < 8; ++k )
  {
    if ( ((pch >> (k * 8)) & 0xff) != 0 )
    {
      anchor_byte = k;
      break;
    }
  }
  if ( anchor_byte < 0 || pch < 0x1000 )
  {
    module.go_types = std::move(fresh);
    return;
  }
  const uint8_t want_byte = uint8_t((pch >> (anchor_byte * 8)) & 0xff);

  go_md_candidate_t candidate;
  int candidates = 0;
  for ( const go_span_t &span : spans )
  {
    if ( span.size < 8 )
      continue;
    for ( size_t p = 0; p + 8 <= span.size; ++p )
    {
      if ( span.data[p] != want_byte )
        continue;
      const int64 hit = span.dest + int64(p);
      const int64 md = (hit - anchor_byte) & ~int64(7);
      if ( (hit - anchor_byte) & 7 ) // moduledata is pointer-aligned
        continue;
      if ( go_read_uint(spans, md, 8) != pch )
        continue;
      if ( !go_md_generic_checks(spans, md, anchor) )
        continue;
      ++candidates;
      const bool s126 = go_md_score_126(spans, md);
      const bool s127 = go_md_score_127(spans, md);
      if ( s126 || s127 )
      {
        candidate = go_md_candidate_t{ md, s126, s127 };
        break;
      }
    }
    if ( candidate.addr != 0 )
      break;
  }

  if ( candidate.addr == 0 )
  {
    fresh.present = false;
    fresh.mismatch_reason = candidates > 0
      ? "go moduledata layout mismatch: no layout scored for the candidate moduledata"
      : "";
    module.go_types = std::move(fresh);
    return;
  }

  const int64 md = candidate.addr;
  fresh.moduledata = uint64(md);
  fresh.types = go_read_uint(spans, md + kGoMdTypes, 8);
  fresh.layout_version = candidate.matched_126 && candidate.matched_127 ? 0
    : candidate.matched_126                                             ? 26
                                                                        : 27;
  if ( fresh.layout_version == 0 )
  {
    fresh.present = false;
    fresh.mismatch_reason = "go moduledata layout mismatch: both layouts scored";
    module.go_types = std::move(fresh);
    return;
  }

  // Cross-check against the resolved profile (when it resolved — the
  // scan runs before the producers custom section may have been parsed).
  const int expected = go_expected_moduledata_version(module.go_profile.profile);
  if ( expected != 0 && expected != fresh.layout_version )
  {
    fresh.present = false;
    fresh.mismatch_reason = "go moduledata layout mismatch: profile expects layout "
      + std::to_string(expected) + ", scored " + std::to_string(fresh.layout_version);
    module.go_types = std::move(fresh);
    return;
  }

  const go_md_layout_t &layout = fresh.layout_version == 26 ? kGoMdLayout126 : kGoMdLayout127;
  fresh.gofunc = go_read_uint(spans, md + layout.gofunc, 8);

  // Descriptor roots.
  if ( fresh.layout_version == 26 )
  {
    fresh.typelinks_ptr = go_read_uint(spans, md + kGoMdLayout126.typelinks_ptr, 8);
    fresh.typelinks_len = uint32_t(go_read_uint(spans, md + kGoMdLayout126.typelinks_len, 8));
    fresh.itablinks_ptr = go_read_uint(spans, md + kGoMdLayout126.itablinks_ptr, 8);
    fresh.itablinks_len = uint32_t(go_read_uint(spans, md + kGoMdLayout126.itablinks_len, 8));
    fresh.descriptors.reserve(fresh.typelinks_len);
    for ( uint32_t i = 0; i < fresh.typelinks_len; ++i )
    {
      const int32_t off = int32_t(go_read_uint(spans, int64(fresh.typelinks_ptr) + i * 4, 4));
      go_type_descriptor_t descriptor;
      if ( !go_decode_descriptor(spans, int64(fresh.types) + off, int64(fresh.types), &descriptor) )
        continue; // gap-cut root: kept out of the layer (fail-closed)
      fresh.descriptors.push_back(std::move(descriptor));
    }
  }
  else
  {
    fresh.typedesclen = go_read_uint(spans, md + kGoMdLayout127.etypes, 8);
    fresh.etypes = go_read_uint(spans, md + kGoMdLayout127.et_b, 8);
    fresh.itaboffset = go_read_uint(spans, md + kGoMdLayout127.et_c, 8);
    fresh.itabsize = go_read_uint(spans, md + kGoMdLayout127.et_d, 8);
    size_t walk_count = 0;
    if ( !go_walk_types_127(spans, int64(fresh.types), int64(fresh.typedesclen), &walk_count) )
    {
      fresh.present = false;
      fresh.mismatch_reason = "go moduledata layout mismatch: types walk desynchronized";
      fresh.descriptors.clear();
      module.go_types = std::move(fresh);
      return;
    }
    fresh.descriptors.reserve(walk_count);
    const int64 end = int64(fresh.types + fresh.typedesclen);
    int64 t = int64(fresh.types) + 8;
    while ( t < end )
    {
      t = (t + 7) & ~int64(7);
      const uint8_t kind = uint8_t(go_read_uint(spans, t + 23, 1));
      if ( !go_kind_valid(kind) )
      {
        t += 8;
        continue;
      }
      const int step = go_descriptor_size(spans, t);
      go_type_descriptor_t descriptor;
      if ( go_decode_descriptor(spans, t, int64(fresh.types), &descriptor) )
        fresh.descriptors.push_back(std::move(descriptor));
      t += step;
    }
  }

  // Eager reference closure. The walk region (1.27) / typelinks
  // roots (1.26) carry only the linker-emitted root descriptors; the named
  // types the display side needs (js.Value, main.Ball, main.Pt) live past
  // it and are reachable only through elem/elem2/field/param chains. The
  // lazy resolver (resolve_go_type_addr) appends them at load time — AFTER
  // blob 't' has been written — so follow every reference transitively
  // here, bounded, and the blob the decompiler decodes already contains
  // the named structs the group naming and til layouts build on. A partial
  // closure (cap hit) is fine: the load-time lazy resolution keeps
  // extending the in-memory layer exactly as before.
  {
    const size_t kClosureDescriptorCap = size_t(1) << 15;
    std::vector<uint64_t> worklist;
    std::set<uint64_t> known;
    const auto push_refs = [&worklist](const go_type_descriptor_t &descriptor)
    {
      if ( descriptor.elem_addr != 0 )
        worklist.push_back(descriptor.elem_addr);
      if ( descriptor.elem2_addr != 0 )
        worklist.push_back(descriptor.elem2_addr);
      if ( descriptor.ptr_to_this != 0 )
        worklist.push_back(descriptor.ptr_to_this);
      for ( const go_type_field_t &field : descriptor.fields )
        if ( field.type_addr != 0 )
          worklist.push_back(field.type_addr);
      for ( const uint64_t param : descriptor.param_addrs )
        if ( param != 0 )
          worklist.push_back(param);
    };
    for ( const go_type_descriptor_t &descriptor : fresh.descriptors )
    {
      known.insert(descriptor.addr);
      push_refs(descriptor);
    }
    while ( !worklist.empty() && fresh.descriptors.size() < kClosureDescriptorCap )
    {
      const uint64_t addr = worklist.back();
      worklist.pop_back();
      if ( addr == 0 || !known.insert(addr).second )
        continue;
      go_type_descriptor_t descriptor;
      if ( !go_decode_descriptor(spans, int64(addr), int64(fresh.types), &descriptor) )
        continue; // unreadable target: not a descriptor, fail-closed
      descriptor.eager_closure = true;
      push_refs(descriptor);
      fresh.descriptors.push_back(std::move(descriptor));
    }
  }

  // ArgInfo (funcdata[5]) per defined function.
  fresh.arginfo.assign(anchor.func_addrs.size(), go_arginfo_t());
  for ( size_t i = 0; i < anchor.func_addrs.size(); ++i )
  {
    const int64 fstruct = int64(anchor.func_addrs[i]);
    if ( fstruct <= 0 )
      continue;
    const uint32_t npcdata = uint32_t(go_read_uint(spans, fstruct + 28, 4));
    const uint8_t nfuncdata = uint8_t(go_read_uint(spans, fstruct + 43, 1));
    if ( nfuncdata <= 5 || npcdata > 65536 )
      continue;
    const uint32_t fd5
      = uint32_t(go_read_uint(spans, fstruct + 44 + int64(npcdata) * 4 + 5 * 4, 4));
    if ( fd5 == 0xffffffffu )
      continue;
    const int32_t args = int32_t(go_read_uint(spans, fstruct + 8, 4));
    go_arginfo_t decoded;
    if ( go_decode_arginfo(spans, int64(fresh.gofunc) + int64(fd5), args, &decoded) )
      fresh.arginfo[i] = std::move(decoded);
  }

  fresh.present = true;
  // The itab layer survives a rescan like the pclntab anchor above
  // (it is re-derived lazily and never serialized in blob 't').
  fresh.itabs = std::move(module.go_types.itabs);
  fresh.itabs_scanned = module.go_types.itabs_scanned;
  module.go_types = std::move(fresh);
}

const go_type_descriptor_t *resolve_go_type_addr(module_t &module, uint64 addr)
{
  if ( module.go_types.present && module.go_types.descriptor_by_addr(addr) != nullptr )
    return module.go_types.descriptor_by_addr(addr);
  if ( !module.go_types.present || addr == 0 )
    return nullptr;
  std::vector<go_span_t> spans;
  build_go_spans(module, &spans);
  go_type_descriptor_t descriptor;
  if ( !go_decode_descriptor(spans, int64(addr), int64(module.go_types.types), &descriptor) )
    return nullptr;
  module.go_types.descriptors.push_back(std::move(descriptor));
  return &module.go_types.descriptors.back();
}

// Itab layer, named-signature anchors, C-presentation rendering.
namespace
{

// tflag bit1: the descriptor name carries one redundant leading '*'
// (the descriptor-name convention: builtin `string` is named `*string`).
constexpr uint8 kGoTFlagExtraStar = 0x2;

// fun[i] == table0_slot << 16 (the funcval PC_F encoding). Resolve a raw
// fun value to a module function index through the statically initialized
// table-0 slots; UINT32_MAX when the low half is nonzero or the slot is
// unknown (dynamic/uninitialized tables stay fail-closed).
uint32_t go_table0_slot_to_fn(const module_t &module, uint64_t fun)
{
  if ( (fun & 0xffffu) != 0 || fun == 0 )
    return UINT32_MAX;
  const uint64_t slot = fun >> 16;
  if ( slot > UINT32_MAX )
    return UINT32_MAX;
  for ( const table_slot_t &entry : module.table_slots )
    if ( entry.table_index == 0 && entry.slot_index == slot )
      return entry.function_index;
  return UINT32_MAX;
}

// Display name of a decoded descriptor: the raw str name with one redundant
// leading '*' stripped when tflagExtraStar is set ("*string" -> "string";
// genuine pointer types like "*errors.errorString" keep their star).
std::string go_descriptor_display_name(const go_type_descriptor_t &descriptor)
{
  std::string name = descriptor.name;
  if ( (descriptor.tflag & kGoTFlagExtraStar) != 0 && !name.empty() && name[0] == '*' )
    name.erase(0, 1);
  return name;
}

} // namespace

const std::vector<go_itab_t> &go_itab_layer(module_t &module)
{
  go_type_info_t &info = module.go_types;
  if ( info.itabs_scanned )
    return info.itabs;
  info.itabs.clear();
  info.itabs_scanned = true;
  if ( !info.present )
    return info.itabs;

  std::vector<go_span_t> spans;
  build_go_spans(module, &spans);
  const int64 types = int64(info.types);
  std::vector<go_itab_t> layer;

  // Decode one itab at `it` into `out`; returns the total size (header +
  // fun words), 0 when the header is unreadable/invalid, -1 for a padding
  // slot (both pointers zero — skip 8 bytes).
  auto decode_itab = [&](int64 it, go_itab_t *out) -> int64_t
  {
    const uint64_t inter = go_read_uint(spans, it, 8);
    const uint64_t con = go_read_uint(spans, it + 8, 8);
    if ( inter == 0 && con == 0 )
      return -1;
    // NOTE: resolve_go_type_addr APPENDS to module.go_types.descriptors, so
    // a pointer into that vector can dangle across the next resolve — hold
    // every descriptor by value.
    const go_type_descriptor_t *inter_raw = resolve_go_type_addr(module, inter);
    const go_type_descriptor_t inter_d = inter_raw != nullptr ? *inter_raw : go_type_descriptor_t();
    const go_type_descriptor_t *con_raw = resolve_go_type_addr(module, con);
    const go_type_descriptor_t con_d = con_raw != nullptr ? *con_raw : go_type_descriptor_t();
    if ( inter_raw == nullptr || con_raw == nullptr || inter_d.kind != kGoKindInterface )
      return 0;
    const uint64_t mlen = go_read_uint(spans, inter_d.addr + 64, 8);
    if ( mlen > 256 )
      return 0;
    out->addr = uint64(it);
    out->inter_addr = inter_d.addr;
    out->type_addr = con_d.addr;
    out->inter_name = go_descriptor_display_name(inter_d);
    out->type_name = go_descriptor_display_name(con_d);
    const int64 mptr = int64(go_read_uint(spans, inter_d.addr + 56, 8));
    for ( uint64_t m = 0; m < mlen; ++m )
    {
      go_itab_method_t method;
      const int32_t name_off = int32_t(go_read_uint(spans, mptr + int64(m) * 8, 4));
      const int32_t typ_off = int32_t(go_read_uint(spans, mptr + int64(m) * 8 + 4, 4));
      if ( name_off > 0 )
        go_read_name(spans, types + name_off, &method.name);
      if ( typ_off > 0 )
        method.func_type_addr = uint64(types + typ_off);
      method.fn_index
        = go_table0_slot_to_fn(module, go_read_uint(spans, it + 24 + int64(m) * 8, 8));
      out->methods.push_back(std::move(method));
    }
    return 24 + int64(mlen) * 8;
  };

  if ( info.layout_version == 27 )
  {
    const int64 begin = types + int64(info.itaboffset);
    const int64 end = int64(info.etypes);
    if ( end <= begin )
      return info.itabs; // region facts already validated; defensive
    int64 it = begin;
    while ( it + 24 <= end )
    {
      it = (it + 7) & ~int64(7);
      if ( it + 24 > end )
        break;
      go_itab_t itab;
      const int64_t size = decode_itab(it, &itab);
      if ( size < 0 )
      {
        it += 8;
        continue;
      }
      if ( size == 0 )
        it += 8; // unreadable header: re-align over the slot, fail-closed
      else
      {
        layer.push_back(std::move(itab));
        it += size;
      }
      if ( layer.size() > 4096 )
        return info.itabs; // defensive cap; region is <= typedesclen-sized
    }
    // The region must be consumed exactly (moduleTypelinks contract).
    if ( it != end )
    {
      info.itabs.clear();
      return info.itabs;
    }
  }
  else
  {
    const int64 ptr = int64(info.itablinks_ptr);
    for ( uint32_t k = 0; k < info.itablinks_len; ++k )
    {
      const int64 it = int64(go_read_uint(spans, ptr + int64(k) * 8, 8));
      if ( it <= 0 )
        continue; // gap-cut entry: skipped, fail-closed
      go_itab_t itab;
      if ( decode_itab(it, &itab) > 0 )
        layer.push_back(std::move(itab));
      if ( layer.size() > 4096 )
        return info.itabs;
    }
  }
  info.itabs = std::move(layer);
  return info.itabs;
}

namespace
{

// Render a type descriptor into its Go display string. Bounded depth; the
// named form wins whenever the descriptor carries a name (the layer
// decodes the str name for every readable root).
bool go_render_type(const std::vector<go_span_t> &spans, module_t &module, int64 addr, int depth,
                    std::string *out)
{
  if ( addr <= 0 || depth > 4 )
    return false;
  const go_type_descriptor_t *resolved = resolve_go_type_addr(module, uint64(addr));
  if ( resolved == nullptr )
    return false;
  // The recursive calls below append to the descriptors vector — hold the
  // descriptor by value so nothing dangles across them.
  const go_type_descriptor_t descriptor = *resolved;
  const std::string name = go_descriptor_display_name(descriptor);
  const bool named = !name.empty() && descriptor.kind != kGoKindPointer
    && descriptor.kind != kGoKindSlice && descriptor.kind != kGoKindArray
    && descriptor.kind != kGoKindMap && descriptor.kind != kGoKindChan;
  if ( named )
  {
    *out = name;
    return true;
  }
  std::string elem;
  switch ( descriptor.kind )
  {
    case kGoKindPointer:
      if ( !go_render_type(spans, module, int64(descriptor.elem_addr), depth + 1, &elem) )
        return false;
      *out = "*" + elem;
      return true;
    case kGoKindSlice:
      if ( !go_render_type(spans, module, int64(descriptor.elem_addr), depth + 1, &elem) )
        return false;
      *out = "[]" + elem;
      return true;
    case kGoKindArray:
      if ( !go_render_type(spans, module, int64(descriptor.elem_addr), depth + 1, &elem) )
        return false;
      *out = "[" + std::to_string(descriptor.array_len) + "]" + elem;
      return true;
    case kGoKindMap:
    {
      std::string key;
      if ( !go_render_type(spans, module, int64(descriptor.elem_addr), depth + 1, &key)
           || !go_render_type(spans, module, int64(descriptor.elem2_addr), depth + 1, &elem) )
        return false;
      *out = "map[" + key + "]" + elem;
      return true;
    }
    case kGoKindChan:
      if ( !go_render_type(spans, module, int64(descriptor.elem_addr), depth + 1, &elem) )
        return false;
      *out = "chan " + elem;
      return true;
    case kGoKindStruct:
      if ( name.empty() )
        return false; // anonymous struct: too wide for this slice, honest skip
      *out = name;
      return true;
    case kGoKindFunc:
      if ( name.empty() )
        return false;
      *out = name;
      return true;
    default:
      if ( name.empty() )
        return false;
      *out = name;
      return true;
  }
}

// The C-presentation name for one signature word: named composites become
// virtual til structs, scalars and structural types stay "" (unrepresentable
// as a single C type — the honest choice given the wasm word layout).
std::string go_c_name_for(const go_type_descriptor_t &descriptor)
{
  const std::string name = go_descriptor_display_name(descriptor);
  if ( name == "string" )
    return "go__string";
  if ( descriptor.kind == kGoKindInterface )
    return name.empty() ? "" : "go__iface";
  if ( descriptor.kind == kGoKindSlice )
    return "go__slice";
  if ( descriptor.kind == kGoKindStruct && !name.empty() )
  {
    // pkg.Type -> go__pkg__Type (the loader sanitizer pattern: dots become
    // underscores under a go__ prefix).
    std::string sanitized = "go__";
    for ( char c : name )
      sanitized.push_back(c == '.' || c == '/' ? '_' : c);
    return sanitized;
  }
  return "";
}

// Decode the param/result list of a FuncType descriptor into display names,
// C names and sizes. Fail-closed on any unreadable piece.
bool go_functype_params(module_t &module, std::uint64_t func_type_addr,
                        std::vector<go_sig_param_t> *params, std::vector<go_sig_param_t> *results,
                        bool *variadic)
{
  // Hold by value: the per-param resolves below append to the same
  // descriptors vector a pointer into it would dangle.
  const go_type_descriptor_t *func_raw = resolve_go_type_addr(module, func_type_addr);
  if ( func_raw == nullptr || func_raw->kind != kGoKindFunc || func_raw->param_addrs.empty() )
    return false;
  const go_type_descriptor_t func = *func_raw;
  std::vector<go_span_t> spans;
  build_go_spans(module, &spans);
  const size_t in = func.in_count;
  const size_t total = func.param_addrs.size();
  if ( in > total )
    return false;
  for ( size_t k = 0; k < total; ++k )
  {
    const go_type_descriptor_t *descriptor = resolve_go_type_addr(module, func.param_addrs[k]);
    if ( descriptor == nullptr )
      return false;
    const go_type_descriptor_t descriptor_copy = *descriptor; // appends below
    go_sig_param_t param;
    if ( !go_render_type(spans, module, int64(descriptor_copy.addr), 0, &param.display) )
      return false;
    param.c_name = go_c_name_for(descriptor_copy);
    param.size = descriptor_copy.size;
    if ( k < in )
      params->push_back(std::move(param));
    else
      results->push_back(std::move(param));
  }
  *variadic = func.variadic;
  return true;
}

// ArgInfo cross-check: params must cover the decoded slots exactly
// (contiguous from offset 0, same sizes in order) and the result bytes must
// close the _func.args gap. Fail-closed.
bool go_shape_matches(const go_arginfo_t &arginfo, const std::vector<go_sig_param_t> &params,
                      const std::vector<go_sig_param_t> &results)
{
  if ( !arginfo.present )
    return false;
  uint64_t cursor = 0;
  size_t slot = 0;
  for ( const go_sig_param_t &param : params )
  {
    if ( param.size == 0 || param.size % 8 != 0 )
      return false; // Go words are 8 bytes on wasm
    for ( uint64_t byte = 0; byte < param.size; byte += 8 )
    {
      if ( slot >= arginfo.slots.size() )
        return false;
      const go_arginfo_slot_t &at = arginfo.slots[slot];
      if ( at.offset != cursor + byte || at.size != 8 )
        return false;
      ++slot;
    }
    cursor += param.size;
  }
  if ( slot != arginfo.slots.size() )
    return false; // trailing slots the params do not explain
  uint64_t result_bytes = 0;
  for ( const go_sig_param_t &result : results )
    result_bytes += result.size;
  return arginfo.args_bytes >= int64(cursor) && uint64(arginfo.args_bytes) - cursor == result_bytes;
}

// Render the final Go signature string: "error.Error() string",
// "(v js.Value) Get(p string) js.Value", "func(this js.Value, args []js.Value) any".
std::string go_render_signature(const go_named_sig_t &sig)
{
  std::string text;
  if ( sig.source == 3 )
    text = "func("; // a bare callback signature
  else if ( !sig.receiver.empty() )
  {
    const size_t method_dot = sig.label.rfind('.');
    text = "(" + sig.receiver + ") " + sig.label.substr(method_dot + 1) + "(";
  }
  else
    text = sig.label + "(";
  if ( !sig.complete )
    return text + "...)"; // name anchor only: never claim an unproven arity
  for ( size_t k = 0; k < sig.params.size(); ++k )
  {
    if ( k != 0 )
      text += ", ";
    text += sig.params[k].display;
    if ( sig.variadic && k + 1 == sig.params.size() )
      text += "...";
  }
  text += ")";
  if ( sig.results.empty() )
    return text;
  if ( sig.results.size() == 1 )
    return text + " " + sig.results[0].display;
  text += " (";
  for ( size_t k = 0; k < sig.results.size(); ++k )
  {
    if ( k != 0 )
      text += ", ";
    text += sig.results[k].display;
  }
  return text + ")";
}

// Known-API table (fail-closed: exact display-name match + shape)
struct known_api_param_t
{
  const char *go; // display type
  std::uint64_t size;
  const char *name; // Go parameter name ("" = not curated; keeps aN)
};

struct known_api_entry_t
{
  const char *name;   // final display name ("syscall/js.Value.Get")
  const char *method; // short method name for the label ("Get")
  const char *recv;   // receiver display ("js.Value") or ""
  known_api_param_t params[4];
  known_api_param_t result; // {nullptr, 0} = no result
  bool variadic;
};

// js.Value is 16 bytes on the go1.26/go1.27 wasm corpora (ref + gcPtr —
// verified against the js.Value struct descriptors of both corpora).
constexpr std::uint64_t kJsValueSize = 16;

// The loader's final display names: name-section/pclntab symbols carrying
// '/' (syscall/js...) pass through sanitize_identifier, so the table keys
// use the sanitized form exactly as entry.cpp sees them.
constexpr known_api_entry_t kGoKnownApi[] = {
  { "syscall_js.Value.Get",
    "Get",
    "js.Value",
    { { "js.Value", kJsValueSize, "this" }, { "string", 16, "prop" } },
    { "js.Value", kJsValueSize, "" },
    false },
  { "syscall_js.Value.Set",
    "Set",
    "js.Value",
    { { "js.Value", kJsValueSize, "this" }, { "string", 16, "method" }, { "any", 16, "value" } },
    { nullptr, 0, nullptr },
    false },
  { "syscall_js.Value.Call",
    "Call",
    "js.Value",
    { { "js.Value", kJsValueSize, "this" }, { "string", 16, "method" }, { "[]any", 24, "args" } },
    { "js.Value", kJsValueSize, "" },
    true },
  { "syscall_js.Value.Invoke",
    "Invoke",
    "js.Value",
    { { "js.Value", kJsValueSize, "this" }, { "[]any", 24, "args" } },
    { "js.Value", kJsValueSize, "" },
    true },
  { "syscall_js.Value.New",
    "New",
    "js.Value",
    { { "js.Value", kJsValueSize, "this" }, { "[]any", 24, "args" } },
    { "js.Value", kJsValueSize, "" },
    true },
  { "syscall_js.Value.Float",
    "Float",
    "js.Value",
    { { "js.Value", kJsValueSize, "this" } },
    { "float64", 8, "" },
    false },
  { "syscall_js.Value.Int",
    "Int",
    "js.Value",
    { { "js.Value", kJsValueSize, "this" } },
    { "int", 8, "" },
    false },
  { "syscall_js.Value.Bool",
    "Bool",
    "js.Value",
    { { "js.Value", kJsValueSize, "this" } },
    { "bool", 1, "" },
    false },
  { "syscall_js.Value.Truthy",
    "Truthy",
    "js.Value",
    { { "js.Value", kJsValueSize, "this" } },
    { "bool", 1, "" },
    false },
  { "syscall_js.Value.String",
    "String",
    "js.Value",
    { { "js.Value", kJsValueSize, "this" } },
    { "string", 16, "" },
    false },
  { "syscall_js.Value.Length",
    "Length",
    "js.Value",
    { { "js.Value", kJsValueSize, "this" } },
    { "int", 8, "" },
    false },
  { "syscall_js.Value.IsUndefined",
    "IsUndefined",
    "js.Value",
    { { "js.Value", kJsValueSize, "this" } },
    { "bool", 1, "" },
    false },
  { "syscall_js.Value.IsNull",
    "IsNull",
    "js.Value",
    { { "js.Value", kJsValueSize, "this" } },
    { "bool", 1, "" },
    false },
  { "syscall_js.Value.Type",
    "Type",
    "js.Value",
    { { "js.Value", kJsValueSize, "this" } },
    { "js.Type", 8, "" },
    false },
  { "fmt.Sprintf",
    "Sprintf",
    "",
    { { "string", 16, "format" }, { "[]any", 24, "args" } },
    { "string", 16, "" },
    true },
};

std::string go_c_name_for_known(const std::string &display)
{
  if ( display == "string" )
    return "go__string";
  if ( display == "js.Value" )
    return "go__js__Value";
  if ( display == "[]any" )
    return "go__slice";
  if ( display == "any" )
    return "go__iface";
  return "";
}

// (go_body_display_name moved below, outside the anonymous namespace —
// declared in types.hpp since the global-renaming walk.)

// Static funcval constants (slot<<16) inside one body, resolved to defined
// function indices. Uses the single decoder over the body opcodes
// (after the local-group preamble, the same layout parse_code walks).
void go_collect_static_funcvals(const module_t &module, const body_t &body,
                                std::vector<std::uint32_t> *fns)
{
  if ( body.size == 0 || body.offset >= module.code.size() )
    return;
  const std::uint8_t *base = module.code.data() + body.offset;
  std::size_t size = body.size;
  // Local-group preamble: [group count][[count LEB][type byte]]*.
  std::size_t pos = 0;
  auto read_uleb = [&](std::uint64_t *value) -> bool
  {
    std::uint64_t result = 0;
    unsigned shift = 0;
    for ( int i = 0; i < 10 && pos < size; ++i )
    {
      const std::uint8_t byte = base[pos++];
      result |= std::uint64_t(byte & 0x7f) << shift;
      if ( (byte & 0x80) == 0 )
      {
        *value = result;
        return true;
      }
      shift += 7;
    }
    return false;
  };
  std::uint64_t groups = 0;
  if ( !read_uleb(&groups) || groups > size )
    return;
  for ( std::uint64_t g = 0; g < groups; ++g )
  {
    std::uint64_t count = 0;
    if ( !read_uleb(&count) || pos >= size )
      return;
    ++pos; // value type byte
    (void)count;
  }
  if ( pos >= size )
    return;
  binary::insn_cursor_t cursor;
  cursor.data = base + pos;
  cursor.size = size - pos;
  binary::decoded_insn_t insn;
  while ( binary::next_insn(cursor, &insn) )
  {
    if ( insn.opcode != 0x42 ) // i64.const
      continue;
    const std::uint32_t fn = go_table0_slot_to_fn(module, insn.imm[0]);
    if ( fn != UINT32_MAX && fn >= module.imported_func_types.size() )
      fns->push_back(fn);
  }
}

// Sanitize into a C identifier fragment (letters, digits, underscore).
std::string go_sanitize_field(const std::string &name)
{
  std::string out;
  for ( char c : name )
    out.push_back((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ? c
                                                                                             : '_');
  while ( out.size() > 1 && out[0] == '_' )
    out.erase(0, 1);
  return out.empty() ? "_" : out;
}

} // namespace

bool go_type_display_name(module_t &module, std::uint64_t type_addr, std::string *out)
{
  if ( !module.go_types.present )
    return false;
  std::vector<go_span_t> spans;
  build_go_spans(module, &spans);
  return go_render_type(spans, module, int64(type_addr), 0, out);
}

bool go_resolve_named_signature(module_t &module, std::size_t body_index,
                                const std::string &function_name, go_named_sig_t *out)
{
  if ( out == nullptr || !module.go_types.present || body_index >= module.bodies.size() )
    return false;
  const uint32 fn_index = uint32(module.imported_func_types.size() + body_index);
  const go_arginfo_t &arginfo = body_index < module.go_types.arginfo.size()
    ? module.go_types.arginfo[body_index]
    : go_arginfo_t();
  const std::vector<go_itab_t> &itabs = go_itab_layer(module);
  go_named_sig_t sig;

  // anchor 1: itab
  for ( const go_itab_t &itab : itabs )
  {
    for ( const go_itab_method_t &method : itab.methods )
    {
      if ( method.fn_index != fn_index || method.name.empty() )
        continue;
      // The fun entry byte-proves this body implements inter.method on
      // itab.type — the NAME anchor always holds.
      go_named_sig_t candidate;
      candidate.source = 1;
      candidate.label = itab.inter_name + "." + method.name;
      candidate.anchor = "itab: " + itab.inter_name + " -> " + itab.type_name;
      const go_type_descriptor_t *recv_raw = resolve_go_type_addr(module, itab.type_addr);
      const go_type_descriptor_t recv = recv_raw != nullptr ? *recv_raw : go_type_descriptor_t();
      bool typed = method.func_type_addr != 0 && recv_raw != nullptr
        && go_functype_params(module, method.func_type_addr, &candidate.params, &candidate.results,
                              &candidate.variadic);
      if ( typed )
      {
        // The interface method type excludes the receiver; the implementing
        // body receives itab.type's value first (a pointer on every corpus
        // itab — the method set is complete).
        go_sig_param_t receiver;
        receiver.display = go_descriptor_display_name(recv);
        receiver.c_name = go_c_name_for(recv);
        receiver.size = recv.size;
        candidate.params.insert(candidate.params.begin(), std::move(receiver));
        if ( go_shape_matches(arginfo, candidate.params, candidate.results) )
        {
          candidate.signature = go_render_signature(candidate);
          *out = std::move(candidate);
          return true;
        }
      }
      else
      {
        // FuncType gap-cut: keep the byte-proven name, drop the typed arity.
        candidate.complete = false;
        candidate.anchor += " (method signature unrecoverable)";
        if ( arginfo.present )
        {
          candidate.signature = go_render_signature(candidate);
          *out = std::move(candidate);
          return true;
        }
      }
    }
  }

  // anchor 2: known-API
  for ( const known_api_entry_t &known : kGoKnownApi )
  {
    if ( function_name != known.name )
      continue;
    sig.source = 2;
    sig.label = known.name;
    sig.receiver = known.recv;
    sig.anchor = "known-API";
    for ( const known_api_param_t &param : known.params )
    {
      if ( param.go == nullptr )
        break;
      go_sig_param_t entry;
      entry.display = param.go;
      entry.c_name = go_c_name_for_known(param.go);
      entry.name = param.name != nullptr ? param.name : "";
      entry.size = param.size;
      sig.params.push_back(std::move(entry));
    }
    if ( known.result.go != nullptr )
    {
      go_sig_param_t entry;
      entry.display = known.result.go;
      entry.c_name = go_c_name_for_known(known.result.go);
      entry.name = known.result.name != nullptr ? known.result.name : "";
      entry.size = known.result.size;
      sig.results.push_back(std::move(entry));
    }
    sig.variadic = known.variadic;
    if ( go_shape_matches(arginfo, sig.params, sig.results) )
    {
      sig.signature = go_render_signature(sig);
      *out = std::move(sig);
      return true;
    }
    return false; // name matched but the shape disagrees: fail-closed
  }

  // anchor 3: syscall/js.FuncOf callback
  {
    // Any body calling syscall/js.FuncOf whose static funcval constants
    // include a defined function with the callback shape.
    uint32 func_of_fn = UINT32_MAX;
    for ( size_t k = 0; k < module.bodies.size(); ++k )
    {
      const std::string candidate = go_body_display_name(module, k);
      if ( candidate == "syscall/js.FuncOf" || candidate == "syscall_js.FuncOf" )
      {
        func_of_fn = uint32(module.imported_func_types.size() + k);
        break;
      }
    }
    if ( func_of_fn != UINT32_MAX )
    {
      for ( const body_t &caller : module.bodies )
      {
        bool calls_func_of = false;
        for ( const go_call_ir_t &call : caller.go_ir.calls )
          if ( call.fn_index == func_of_fn )
          {
            calls_func_of = true;
            break;
          }
        if ( !calls_func_of )
          continue;
        std::vector<std::uint32_t> constants;
        go_collect_static_funcvals(module, caller, &constants);
        std::size_t hits = 0;
        uint32 hit = UINT32_MAX;
        for ( uint32 candidate : constants )
        {
          const size_t candidate_body = size_t(candidate) - module.imported_func_types.size();
          const go_arginfo_t &candidate_info = candidate_body < module.go_types.arginfo.size()
            ? module.go_types.arginfo[candidate_body]
            : go_arginfo_t();
          // The callback shape: this js.Value (16) + args []js.Value (24)
          // in, any (16) out.
          if ( candidate_info.present && candidate_info.args_bytes == 56
               && candidate_info.slots.size() == 5 )
          {
            ++hits;
            hit = candidate;
          }
        }
        if ( hits == 1 && hit == fn_index )
        {
          sig.source = 3;
          sig.label = "syscall/js.FuncOf callback";
          sig.anchor = "js.FuncOf callback passed at "
            + go_body_display_name(module, size_t(&caller - module.bodies.data()));
          go_sig_param_t p1, p2, r1;
          p1.display = "js.Value";
          p1.c_name = "go__js__Value";
          p1.size = kJsValueSize;
          p2.display = "[]js.Value";
          p2.c_name = "go__slice";
          p2.size = 24;
          r1.display = "any";
          r1.c_name = "go__iface";
          r1.size = 16;
          sig.params = { std::move(p1), std::move(p2) };
          sig.results = { std::move(r1) };
          if ( go_shape_matches(arginfo, sig.params, sig.results) )
          {
            sig.signature = go_render_signature(sig);
            *out = std::move(sig);
            return true;
          }
          return false;
        }
      }
    }
  }

  // anchor 4: method symbol + descriptor
  {
    // pkg.__T_.m (pointer receiver) / pkg.T.m (value receiver): the trailing
    // method name splits off, the type must exist as a descriptor, its
    // UncommonType must list the method, and the UncommonType Tfn slot must
    // resolve back to THIS body.
    const size_t dot = function_name.rfind('.');
    if ( dot == std::string::npos || dot == 0 )
      return false;
    const std::string method = function_name.substr(dot + 1);
    const std::string owner = function_name.substr(0, dot);
    const size_t seg_dot = owner.rfind('.');
    const std::string segment = owner.substr(seg_dot == std::string::npos ? 0 : seg_dot + 1);
    std::string want;
    bool pointer_receiver = false;
    if ( segment.size() > 3 && segment.compare(0, 2, "__") == 0 && segment.back() == '_' )
    {
      // `__T_` tail: pointer receiver on pkg.T.
      const std::string pkg = owner.substr(0, seg_dot);
      want = pkg + (pkg.empty() ? "" : ".") + segment.substr(2, segment.size() - 3);
      pointer_receiver = true;
    }
    else if ( !segment.empty() && seg_dot != std::string::npos )
      want = owner; // value receiver: the owner IS the type name
    else
      return false; // no package-qualified owner: not a method symbol
    const go_type_descriptor_t *type = nullptr;
    for ( const go_type_descriptor_t &descriptor : module.go_types.descriptors )
    {
      // The eager reference closure puts struct types like main.Ball
      // into the set directly, stranding the anchor on the struct's own
      // (often empty) method table — the methods live on the `*T` root's
      // UncommonType. Pre-closure those structs were reachable only through
      // the pointer search below; keep exactly that dynamic here.
      if ( descriptor.eager_closure )
        continue;
      if ( go_descriptor_display_name(descriptor) == want )
      {
        type = &descriptor;
        break;
      }
    }
    if ( type == nullptr )
    {
      // The struct itself may live past the walk region — the `*T` pointer
      // root still proves the type (main.Ball behind *main.Ball).
      for ( const go_type_descriptor_t &descriptor : module.go_types.descriptors )
        if ( go_descriptor_display_name(descriptor) == "*" + want
             && descriptor.kind == kGoKindPointer )
        {
          type = &descriptor;
          break;
        }
    }
    if ( type == nullptr )
      return false;
    // The wasm build may place the method table on either side of the
    // T/*T pair — pointer-receiver methods live on *T's UncommonType
    // (*main.Ball carries draw/update) while value-receiver wrappers sit
    // on the struct side, and the mtyp of the wrapper entry includes the
    // receiver word the ArgInfo slot list carries. Search BOTH the found
    // descriptor and its ptr_to_this / elem counterpart (which also
    // reaches structs that live past the walk region, like main.Ball
    // behind *main.Ball). Both are held by value: the second resolve
    // appends to the vector. The counterpart is ALWAYS resolved
    // stopping at the first side's (often empty or receiver-less) table
    // stranded value methods like (main.devNull) Write.
    const go_type_descriptor_t *first_raw = resolve_go_type_addr(module, type->addr);
    go_type_descriptor_t search[2] = { go_type_descriptor_t(), go_type_descriptor_t() };
    if ( first_raw != nullptr )
      search[0] = *first_raw;
    const std::uint64_t other = first_raw != nullptr && search[0].ptr_to_this != 0
      ? search[0].ptr_to_this
      : (first_raw != nullptr && search[0].kind == kGoKindPointer ? search[0].elem_addr : 0);
    if ( other != 0 && other != search[0].addr )
    {
      const go_type_descriptor_t *other_raw = resolve_go_type_addr(module, other);
      if ( other_raw != nullptr && other_raw->addr != search[0].addr )
        search[1] = *other_raw;
    }
    std::vector<go_span_t> spans;
    build_go_spans(module, &spans);
    for ( int pass = 0; pass < 2; ++pass )
    {
      const go_type_descriptor_t &resolved = search[pass];
      if ( (resolved.tflag & kGoTFlagUncommon) == 0 )
        continue;
      const int64 uncommon
        = int64(resolved.addr) + go_descriptor_fixed_size(spans, int64(resolved.addr));
      const uint16_t mcount = uint16_t(go_read_uint(spans, uncommon + 4, 2));
      const uint32_t moff = uint32_t(go_read_uint(spans, uncommon + 8, 4));
      if ( mcount > 256 )
        continue;
      for ( uint16_t m = 0; m < mcount; ++m )
      {
        const int64 entry = uncommon + moff + int64(m) * 16;
        const int32_t name_off = int32_t(go_read_uint(spans, entry, 4));
        const int32_t typ_off = int32_t(go_read_uint(spans, entry + 4, 4));
        const int32_t tfn = int32_t(go_read_uint(spans, entry + 12, 4));
        std::string entry_name;
        if ( name_off > 0 )
          go_read_name(spans, int64(module.go_types.types) + name_off, &entry_name);
        if ( entry_name != method )
          continue;
        // This table's entry may belong to the OTHER side of the
        // T/*T pair (the value table vs the pointer wrapper table) — a
        // mismatch or an unprovable shape here must fall through to the
        // counterpart pass, not reject the whole anchor.
        if ( tfn <= 0 || go_table0_slot_to_fn(module, uint64_t(uint32_t(tfn)) << 16) != fn_index )
          break;
        sig.source = 4;
        sig.label = function_name;
        // 3.4: the receiver display keeps the pointer bit the `__T_` name
        // mangling proved — `main.__Ball_.draw` is a method on *main.Ball,
        // so the comment must read `(*main.Ball) draw(...)`, not the value
        // form. The mangled name is the ground truth here (which T/*T side
        // the UncommonType entry was found on is a search-order detail).
        sig.receiver = pointer_receiver ? "*" + want : want;
        sig.anchor = "method of " + sig.receiver + " (type descriptor)";
        // mtyp gives the full signature INCLUDING the receiver.
        if ( typ_off > 0
             && go_functype_params(module, uint64(int64(module.go_types.types) + typ_off),
                                   &sig.params, &sig.results, &sig.variadic)
             && go_shape_matches(arginfo, sig.params, sig.results) )
        {
          sig.signature = go_render_signature(sig);
          *out = std::move(sig);
          return true;
        }
        // mtyp gap-cut (or a partially readable type): the receiver is
        // still proven — the leading ArgInfo slots must cover exactly the
        // receiver word(s) before a partial signature renders.
        if ( arginfo.present && !arginfo.slots.empty() )
        {
          const std::uint64_t receiver_bytes
            = search[pass].kind == kGoKindPointer ? 8 : search[pass].size;
          bool receiver_ok = receiver_bytes > 0 && receiver_bytes % 8 == 0;
          for ( std::uint64_t byte = 0; receiver_ok && byte < receiver_bytes; byte += 8 )
          {
            const std::size_t slot_index = size_t(byte / 8);
            if ( slot_index >= arginfo.slots.size() || arginfo.slots[slot_index].offset != byte
                 || arginfo.slots[slot_index].size != 8 )
              receiver_ok = false;
          }
          if ( receiver_ok )
          {
            sig.complete = false;
            sig.anchor += " (method signature unrecoverable)";
            sig.signature = go_render_signature(sig);
            *out = std::move(sig);
            return true;
          }
        }
        break; // receiver proven but nothing presentable from THIS table
      }
    }
  }
  return false;
}

bool go_c_struct_for_param(module_t &module, const go_sig_param_t &param, go_c_struct_t *out)
{
  if ( out == nullptr || param.c_name.empty() )
    return false;
  out->c_name = param.c_name;
  out->fields.clear();
  out->size = 0;
  if ( param.c_name == "go__string" )
  {
    out->size = 16;
    out->fields = { { "ptr", "__int64", 0, 8 }, { "len", "__int64", 8, 8 } };
    return true;
  }
  if ( param.c_name == "go__slice" )
  {
    out->size = 24;
    out->fields
      = { { "ptr", "__int64", 0, 8 }, { "len", "__int64", 8, 8 }, { "cap", "__int64", 16, 8 } };
    return true;
  }
  if ( param.c_name == "go__iface" )
  {
    out->size = 16;
    out->fields = { { "itab", "__int64", 0, 8 }, { "data", "__int64", 8, 8 } };
    return true;
  }
  // Named struct: rebuild from the descriptor by display name. The struct
  // itself may live past the walk region — reach it through the in-region
  // `*T` pointer's elem when the direct root is missing. Everything is held
  // by value: resolve_go_type_addr appends to the descriptors vector.
  if ( !module.go_types.present )
    return false;
  const go_type_descriptor_t *found = nullptr;
  std::uint64_t via_elem = 0;
  const std::string want = param.display;
  for ( const go_type_descriptor_t &descriptor : module.go_types.descriptors )
  {
    const std::string display = go_descriptor_display_name(descriptor);
    if ( display == want && descriptor.kind == kGoKindStruct )
    {
      found = &descriptor;
      break;
    }
    if ( display == "*" + want && descriptor.kind == kGoKindPointer && via_elem == 0 )
      via_elem = descriptor.elem_addr;
  }
  go_type_descriptor_t type = go_type_descriptor_t();
  if ( found != nullptr )
    type = *found;
  else if ( via_elem != 0 )
  {
    const go_type_descriptor_t *elem = resolve_go_type_addr(module, via_elem);
    if ( elem != nullptr && elem->kind == kGoKindStruct
         && go_descriptor_display_name(*elem) == want )
      type = *elem;
    else
      return false;
  }
  else
    return false;
  if ( type.kind != kGoKindStruct || type.fields.empty() )
    return false;
  out->size = type.size;
  // Two passes: raw (offset, type_addr, name) first, widths second — a
  // gap-cut field TYPE must not kill the layout when the offset delta
  // proves its width (the 0-size `_ [0]func()` of js.Value drops out).
  struct raw_field_t
  {
    std::string name;
    std::uint64_t offset;
    std::uint64_t type_addr;
  };
  std::vector<raw_field_t> raw;
  // reserve keeps the tidy container-growth warning honest without
  // changing behavior.
  raw.reserve(type.fields.size());
  for ( const go_type_field_t &field : type.fields )
    raw.push_back({ field.name, field.offset, field.type_addr });
  std::sort(raw.begin(), raw.end(),
            [](const raw_field_t &a, const raw_field_t &b) { return a.offset < b.offset; });
  std::uint64_t cursor = 0;
  for ( size_t k = 0; k < raw.size(); ++k )
  {
    const raw_field_t &field = raw[k];
    const std::uint64_t next = k + 1 < raw.size() ? raw[k + 1].offset : out->size;
    if ( field.offset > cursor )
      out->fields.push_back(
        { "pad" + std::to_string(cursor), "__int64", cursor, field.offset - cursor });
    const go_type_descriptor_t *field_type = resolve_go_type_addr(module, field.type_addr);
    const std::uint64_t width
      = field_type != nullptr ? field_type->size : (next > field.offset ? next - field.offset : 0);
    if ( width == 0 )
      continue; // 0-size field ([0]func()): occupies nothing
    go_c_field_t rendered;
    rendered.name
      = !field.name.empty() ? go_sanitize_field(field.name) : "f" + std::to_string(field.offset);
    if ( field_type != nullptr )
    {
      const std::string field_c = go_c_name_for(*field_type);
      if ( !field_c.empty() )
        rendered.c_type = "struct " + field_c;
      else if ( field_type->kind == 14 ) // float64
        rendered.c_type = "double";
      else if ( field_type->kind == 13 ) // float32
        rendered.c_type = "float";
      else if ( field_type->size == 8 )
        rendered.c_type = "__int64";
      else if ( field_type->size == 4 )
        rendered.c_type = "int";
      else if ( field_type->size == 1 )
        rendered.c_type = "bool";
      else
        rendered.c_type = "__int64"; // word-sized aggregate: one word
    }
    else
      rendered.c_type = "__int64"; // width proven by the offset delta only
    rendered.offset = field.offset;
    rendered.size = width;
    out->fields.push_back(std::move(rendered));
    cursor = field.offset + width;
  }
  if ( cursor < out->size )
    out->fields.push_back(
      { "pad" + std::to_string(cursor), "__int64", cursor, out->size - cursor });
  return true;
}

// Typed receivers: resolve the proven receiver of an incomplete
// method signature to its C parameter form. The receiver display came from
// the mangled-name flag (anchor 4 / 3.4); the descriptor lookup mirrors
// go_c_struct_for_param's named-struct path (direct root, then the `*T`
// pointer's elem for structs past the walk region).
bool go_receiver_c_param(module_t &module, const go_named_sig_t &sig, bool *is_pointer,
                         go_sig_param_t *out)
{
  if ( out == nullptr || is_pointer == nullptr || !module.go_types.present || sig.receiver.empty() )
    return false;
  const bool pointer = sig.receiver[0] == '*';
  const std::string want = pointer ? sig.receiver.substr(1) : sig.receiver;
  if ( want.empty() )
    return false;
  const go_type_descriptor_t *found = nullptr;
  std::uint64_t via_elem = 0;
  for ( const go_type_descriptor_t &descriptor : module.go_types.descriptors )
  {
    const std::string display = go_descriptor_display_name(descriptor);
    if ( display == want && descriptor.kind == kGoKindStruct )
    {
      found = &descriptor;
      break;
    }
    if ( display == "*" + want && descriptor.kind == kGoKindPointer && via_elem == 0 )
      via_elem = descriptor.elem_addr;
  }
  go_sig_param_t param;
  param.display = want;
  if ( found != nullptr )
    param.c_name = go_c_name_for(*found);
  else if ( via_elem != 0 )
  {
    // The struct lives past the walk region — its `*T` pointer root still
    // resolves it (resolve_go_type_addr holds everything by value).
    const go_type_descriptor_t *elem = resolve_go_type_addr(module, via_elem);
    if ( elem != nullptr && elem->kind == kGoKindStruct
         && go_descriptor_display_name(*elem) == want )
      param.c_name = go_c_name_for(*elem);
  }
  if ( param.c_name.empty() )
    return false; // no descriptor: keep the proven raw scalar
  go_c_struct_t layout;
  if ( !go_c_struct_for_param(module, param, &layout) || layout.fields.empty() )
    return false; // unbuildable layout: keep the raw scalar
  param.c_name = layout.c_name;
  param.size = pointer ? 8 : layout.size;
  *is_pointer = pointer;
  *out = std::move(param);
  return true;
}

bool go_xf_single_result(const module_t &module, std::size_t body_index,
                         const std::string &function_name, go_xf_result_info_t *out)
{
  if ( out == nullptr || !module.go_provenance || !module.go_types.present
       || body_index >= module.bodies.size() )
    return false;
  const body_t &body = module.bodies[body_index];
  const go_function_ir_t &ir = body.go_ir;
  // Gate 1: read-coverage — the frame evidence proves result slots
  // AND every recorded caller reads exactly that window post-call.
  if ( !ir.present || !ir.frame_valid(body.size) || !ir.results_caller_proven() || ir.ret.empty() )
    return false;
  std::vector<go_result_slot_t> slots;
  std::vector<unsigned char> types;
  if ( !go_result_slots_sorted(ir, &slots) || !go_result_slot_types(ir, &types)
       || slots.size() != types.size() || slots.size() < 2 )
    return false; // a single slot already returns by value today
  // Gate 4: every slot maps onto the wasm multi-return register file
  // (mirror of multi_result_word_count without the Hex-Rays dependency).
  const auto slot_width = [](uchar type) -> std::uint32_t
  {
    switch ( type )
    {
      case 0x7F:
      case 0x7D:
      case 0x70:
      case 0x6F: return 4;
      case 0x7E:
      case 0x7C: return 8;
      default: return 0;
    }
  };
  std::uint32_t words = 0;
  std::uint32_t references = 0;
  std::uint32_t extent = slots.front().sp_offset; // sorted: front is the lowest
  for ( size_t k = 0; k < slots.size(); ++k )
  {
    if ( slots[k].width != slot_width(types[k]) )
      return false; // slot width disagrees with the display type
    if ( (types[k] == 0x70 || types[k] == 0x6F) && ++references > 1 )
      return false;
    words += slots[k].width / 4;
    if ( words > 6 )
      return false; // past the multi-return register budget: keep sret
    if ( slots[k].sp_offset != extent )
      return false; // the slots must tile [min_offset, extent) exactly
    extent = slots[k].sp_offset + slots[k].width;
  }
  // Gate 2: a complete signature anchor proving exactly ONE named composite
  // result whose byte size equals the tiled extent.
  go_named_sig_t sig;
  if ( !go_resolve_named_signature(const_cast<module_t &>(module), body_index, function_name, &sig)
       || !sig.complete || sig.results.size() != 1 )
    return false;
  const go_sig_param_t &result = sig.results[0];
  if ( result.c_name.empty() || result.size != extent - slots.front().sp_offset )
    return false;
  go_c_struct_t layout;
  if ( !go_c_struct_for_param(const_cast<module_t &>(module), result, &layout)
       || layout.fields.empty() || layout.size != result.size )
    return false;
  // Gate 3: the incoming ArgInfo expands (the Step C parameter evidence).
  if ( body_index >= module.go_types.arginfo.size() )
    return false;
  std::vector<go_shape_param_t> shape;
  if ( !go_arginfo_shape_params(module.go_types.arginfo[body_index], &shape) || shape.empty() )
    return false;
  // Gate 5: no non-frame caller — the retired register-pointer call lowering
  // is the only world where a phantom sret register slot could be bound by a
  // caller argument; frame callers pass by-value window cells and never
  // touch the register file (structurally kNeutral). Also require the
  // unwind-exit br_if leg every Go call carries (the raw i32 result's only
  // consumer — the x=f call site stops synthesizing it).
  const std::uint32_t fn_index = uint32(module.imported_func_types.size() + body_index);
  for ( const body_t &caller : module.bodies )
  {
    if ( !caller.go_ir.present )
      continue;
    for ( const go_call_ir_t &call : caller.go_ir.calls )
    {
      if ( call.fn_index != fn_index )
        continue;
      if ( call.br_offset == UINT32_MAX )
        return false;
      if ( !caller.go_ir.frame_valid(caller.size) )
        return false;
    }
  }
  out->min_offset = slots.front().sp_offset;
  out->extent = extent;
  out->c_name = layout.c_name;
  out->slot_types = types;
  return true;
}

// The single-source layout builder. See the types.hpp block
// comment for the tier contract. Everything here is fail-closed: a body
// without ArgInfo keeps kNone, a broken encoding keeps kNone, and tier (a)
// additionally requires the anchored signature to pair with the shape
// entry-for-entry (same count, same byte ranges) so the record never
// re-introduces the positional join this pass exists to remove.
void go_build_func_layouts(module_t &module,
                           const std::function<std::string(std::size_t)> &body_name,
                           go_layout_census_t *census)
{
  go_layout_census_t counted;
  if ( census == nullptr )
    census = &counted;
  else
    *census = go_layout_census_t();
  module.go_types.layouts.clear();
  if ( !module.go_types.present || module.bodies.empty() )
  {
    module.go_types.layout_census = *census;
    return;
  }
  // Freeze the wire set: the blob must keep carrying exactly the descriptor
  // set the pre- writer serialized (written right after scan_go_types
  // before any lazy resolve appended). Tier-(a) anchors resolve past this
  // snapshot into module.go_types.descriptors; their names ride the record
  // (c_name + fields materialized below), so the tail is self-contained.
  if ( module.go_types.blob_descriptor_cap == 0 )
    module.go_types.blob_descriptor_cap = module.go_types.descriptors.size();
  go_type_info_t scan_layer;
  scan_layer.present = true;
  scan_layer.descriptors = module.go_types.descriptors;
  // The evidence tier scans exactly what the decoded blob carried.
  const auto scan_by_addr = [&scan_layer](std::uint64_t addr) -> const go_type_descriptor_t *
  {
    return scan_layer.descriptor_by_addr(addr);
  };
  module.go_types.layouts.resize(module.bodies.size());
  for ( std::size_t i = 0; i < module.bodies.size(); ++i )
  {
    ++census->total;
    const go_arginfo_t &arginfo
      = i < module.go_types.arginfo.size() ? module.go_types.arginfo[i] : go_arginfo_t();
    if ( !arginfo.present || arginfo.slots.empty()
         || arginfo.group_starts.size() != arginfo.slots.size() )
    {
      ++census->skip_no_arginfo;
      continue;
    }
    // The (b)-tier shape base: the SAME expansion the call-site walk
    // applies, with one ordering twist — one-word aggregates stay groups
    // until the tier decision: a tier-(a) record keeps them typed (the
    // anchored signature names the composite and the by-value Step C argloc
    // can carry it), while tiers (b)/(c) demote them to scalar words exactly
    // like the old call-site display walk (a one-word pointer argument
    // would lose the value).
    std::vector<go_layout_param_t> params;
    std::vector<bool> demotable; // one-word aggregate, demoted unless tier (a)
    bool shape_ok = true;
    {
      std::size_t k = 0;
      while ( k < arginfo.slots.size() && shape_ok )
      {
        const go_arginfo_slot_t &slot = arginfo.slots[k];
        if ( slot.depth == 0 )
        {
          if ( arginfo.group_starts[k] != 0 )
          {
            shape_ok = false; // a flagged scalar: impossible encoding
            break;
          }
          go_layout_param_t param;
          param.is_group = false;
          param.offset = slot.offset;
          param.bytes = slot.size;
          param.type = slot.size == 8 ? std::uint8_t(0x7E) : std::uint8_t(0x7F);
          params.push_back(std::move(param));
          demotable.push_back(false);
          ++k;
          continue;
        }
        if ( arginfo.group_starts[k] == 0 )
        {
          shape_ok = false; // depth>=1 outside a group: cannot happen
          break;
        }
        std::size_t end = k + 1;
        while ( end < arginfo.slots.size() && arginfo.slots[end].depth >= 1
                && arginfo.group_starts[end] == 0 )
          ++end;
        const std::uint32_t group_offset = arginfo.slots[k].offset;
        const std::uint32_t group_extent
          = arginfo.slots[end - 1].offset + arginfo.slots[end - 1].size - group_offset;
        go_layout_param_t param;
        param.is_group = true;
        param.offset = group_offset;
        param.bytes = group_extent;
        params.push_back(std::move(param));
        demotable.push_back(group_extent <= 8);
        k = end;
      }
    }
    // NOTE: no tiling sanity here — Step C never applied one (it reads the
    // expansion directly), and the call-site walk keeps its own per-call
    // tiling gate, so a non-contiguous slot list still records a shape the
    // prototype path can use while every call site fails closed exactly as
    // before.
    if ( !shape_ok || params.empty() )
    {
      ++census->skip_shape;
      continue;
    }
    // The loader's name stream comes through the callback (name section
    // -> pclntab -> export, deduplicated in defined-body order — exactly
    // entry.cpp's function_name sequence), so the anchors see the same
    // names the prototype path resolves with.
    const std::string name = body_name != nullptr ? body_name(i) : std::string();
    go_func_layout_t &layout = module.go_types.layouts[i];
    // tier (a): anchored FuncType
    go_named_sig_t sig;
    if ( go_resolve_named_signature(module, i, name, &sig) && sig.complete
         && sig.params.size() == params.size() )
    {
      bool paired = true;
      std::uint32_t cursor = 0;
      for ( std::size_t p = 0; paired && p < params.size(); ++p )
      {
        if ( params[p].offset != cursor || params[p].bytes != sig.params[p].size )
          paired = false;
        cursor += params[p].bytes;
      }
      if ( paired )
      {
        bool typed = true;
        for ( std::size_t p = 0; typed && p < params.size(); ++p )
        {
          go_layout_param_t &param = params[p];
          if ( !param.is_group )
            continue; // scalars keep the shape type/width
          const go_sig_param_t &source = sig.params[p];
          if ( source.c_name.empty() )
          {
            typed = false; // a group the anchored type cannot spell
            break;
          }
          param.c_name = source.c_name;
          go_c_struct_t cstr;
          if ( go_c_struct_for_param(module, source, &cstr) && cstr.size == param.bytes )
            param.fields = std::move(cstr.fields);
        }
        if ( typed )
        {
          // Results tile the _func.args gap right after the params
          // (go_shape_matches proved the byte arithmetic).
          std::uint32_t result_offset = 0;
          if ( !params.empty() )
            result_offset = params.back().offset + params.back().bytes;
          for ( const go_sig_param_t &result : sig.results )
          {
            go_layout_param_t entry;
            entry.is_group = result.size > 8;
            entry.offset = result_offset;
            entry.bytes = std::uint32_t(result.size);
            entry.c_name = result.c_name;
            layout.results.push_back(std::move(entry));
            result_offset += std::uint32_t(result.size);
          }
          layout.source = go_layout_source_t::kDescriptor;
          layout.params = std::move(params);
          layout.variadic = sig.variadic;
          layout.anchor = sig.anchor;
          ++census->descriptor;
          continue;
        }
      }
    }
    // Not tier (a): apply the call-site display demotion (one-word
    // aggregates become scalar words — tiers (b)/(c) render exactly like
    // the pre- call-site walk).
    for ( std::size_t p = 0; p < params.size(); ++p )
    {
      if ( !demotable[p] || !params[p].is_group )
        continue;
      params[p].is_group = false;
      params[p].type = params[p].bytes == 8 ? std::uint8_t(0x7E) : std::uint8_t(0x7F);
    }
    // tier (c) naming: size-agreeing descriptors (the old scan)
    // Ported verbatim from collect_go_call_shapes so tier (c) reproduces
    // the previous display for every callee without an anchored record —
    // Only re-ranks the source, it must not change the fallback names.
    bool any_named = false;
    {
      const std::uint32_t in_extent = arginfo.slots.back().offset + arginfo.slots.back().size;
      const int out_bytes = arginfo.args_bytes - int(in_extent);
      int agreeing = 0;
      std::vector<std::string> group_names(params.size());
      std::vector<bool> group_conflict(params.size(), false);
      std::vector<const go_type_descriptor_t *> group_descriptors(params.size(), nullptr);
      for ( const go_type_descriptor_t &descriptor : scan_layer.descriptors )
      {
        if ( descriptor.kind != kGoKindFunc || descriptor.in_count != params.size()
             || descriptor.param_addrs.size() < params.size() )
          continue;
        bool sizes_ok = true;
        std::uint64_t out_sum = 0;
        for ( std::size_t p = 0; p < params.size() && sizes_ok; ++p )
        {
          const go_type_descriptor_t *param_type = scan_by_addr(descriptor.param_addrs[p]);
          if ( param_type == nullptr || param_type->size != params[p].bytes )
            sizes_ok = false;
        }
        for ( std::size_t p = params.size(); p < descriptor.param_addrs.size() && sizes_ok; ++p )
        {
          const go_type_descriptor_t *result_type = scan_by_addr(descriptor.param_addrs[p]);
          if ( result_type == nullptr )
            sizes_ok = false;
          else
            out_sum += result_type->size;
        }
        if ( !sizes_ok || out_bytes < 0 || out_sum != std::uint64_t(out_bytes) )
          continue;
        ++agreeing;
        for ( std::size_t p = 0; p < params.size(); ++p )
        {
          if ( !params[p].is_group )
            continue;
          const go_type_descriptor_t *param_type = scan_by_addr(descriptor.param_addrs[p]);
          if ( param_type == nullptr )
            continue;
          const std::string c_name = go_descriptor_c_name(*param_type);
          if ( c_name.empty() )
            continue;
          if ( group_names[p].empty() )
          {
            group_names[p] = c_name;
            group_descriptors[p] = param_type;
          }
          else if ( group_names[p] != c_name )
            group_conflict[p] = true;
        }
      }
      for ( std::size_t p = 0; p < params.size(); ++p )
      {
        go_layout_param_t &param = params[p];
        if ( !param.is_group )
          continue;
        std::string c_name;
        if ( agreeing > 0 && !group_conflict[p] )
          c_name = group_names[p];
        if ( !c_name.empty() )
        {
          param.c_name = c_name;
          any_named = true;
          // A NAMED struct group keeps its go__X name only when the
          // descriptor walk yields a closed layout (fail-closed back to the
          // extent name at emit time otherwise — same rule as the filter).
          if ( c_name != "go__string" && c_name != "go__iface" && c_name != "go__slice"
               && group_descriptors[p] != nullptr )
          {
            std::vector<go_c_field_t> fields;
            std::uint64_t struct_size = 0;
            if ( go_descriptor_struct_fields(scan_layer, *group_descriptors[p], &fields,
                                             &struct_size)
                 && struct_size == param.bytes )
              param.fields = std::move(fields);
          }
        }
      }
      // A VALUE-receiver method call names its first (receiver) group from
      // the named struct descriptor when exactly one kind-25 descriptor
      // with that Go name exists and its size matches the receiver extent.
      // The callee's own symbol IS the anchor (mangled form — value
      // receivers are already canonical, pointer wrappers pass a scalar
      // and never reach here). Ported verbatim from the filter.
      if ( !params.empty() && params[0].is_group && params[0].fields.empty() )
      {
        const size_t first_dot = name.find('.');
        const size_t last_dot = name.rfind('.');
        if ( first_dot != std::string::npos && last_dot != first_dot
             && name.find('.', first_dot + 1) == last_dot )
        {
          const std::string pkg = name.substr(0, first_dot);
          const std::string type_name = name.substr(first_dot + 1, last_dot - first_dot - 1);
          const size_t pkg_tail_pos = pkg.rfind('_');
          const std::string pkg_tail
            = pkg_tail_pos == std::string::npos ? pkg : pkg.substr(pkg_tail_pos + 1);
          const bool pointer_form
            = type_name.size() > 3 && type_name.compare(0, 2, "__") == 0 && type_name.back() == '_';
          const bool plain_type = !type_name.empty()
            && type_name.find_first_not_of("abcdefghijklmnopqrstuvwxyz"
                                           "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_")
              == std::string::npos;
          if ( !pointer_form && plain_type && !pkg.empty() )
          {
            std::string candidates[2];
            candidates[0] = pkg;
            candidates[0] += ".";
            candidates[0] += type_name;
            candidates[1] = pkg_tail;
            candidates[1] += ".";
            candidates[1] += type_name;
            const go_type_descriptor_t *receiver = nullptr;
            int matches = 0;
            for ( const go_type_descriptor_t &descriptor : scan_layer.descriptors )
            {
              if ( descriptor.kind != kGoKindStruct || descriptor.size != params[0].bytes )
                continue;
              const std::string display = go_descriptor_type_name(descriptor);
              if ( display != candidates[0] && display != candidates[1] )
                continue;
              ++matches;
              receiver = &descriptor;
            }
            std::vector<go_c_field_t> fields;
            std::uint64_t struct_size = 0;
            if ( matches == 1 && receiver != nullptr
                 && go_descriptor_struct_fields(scan_layer, *receiver, &fields, &struct_size)
                 && struct_size == params[0].bytes )
            {
              params[0].c_name = go_descriptor_c_name(*receiver);
              params[0].fields = std::move(fields);
              any_named = true;
            }
          }
        }
      }
    }
    layout.source = any_named ? go_layout_source_t::kEvidence : go_layout_source_t::kArgInfo;
    layout.params = std::move(params);
    layout.anchor = any_named ? "arginfo+descriptor-agreement" : "arginfo";
    if ( census != nullptr )
    {
      if ( any_named )
        ++census->evidence;
      else
        ++census->arginfo;
    }
  }
  module.go_types.layout_census = *census;
}

// Pure display helpers over a decoded layer (blob 't') — see the
// types.hpp block comment. The layouts and names mirror the loader-side
// rendering exactly (go_render_type / go_c_name_for / go_c_struct_for_param)
// so a til struct registered from either side carries the same definition.
std::string go_display_type_name(const go_type_info_t &layer, std::uint64_t type_addr, int depth)
{
  if ( type_addr == 0 || depth > 4 )
    return "";
  const go_type_descriptor_t *resolved = layer.descriptor_by_addr(type_addr);
  if ( resolved == nullptr )
    return "";
  // Hold by value: nothing appends to the layer here, but the copy keeps the
  // rule identical to the loader-side renderer.
  const go_type_descriptor_t descriptor = *resolved;
  std::string name = go_descriptor_display_name(descriptor);
  const bool named = !name.empty() && descriptor.kind != kGoKindPointer
    && descriptor.kind != kGoKindSlice && descriptor.kind != kGoKindArray
    && descriptor.kind != kGoKindMap && descriptor.kind != kGoKindChan;
  if ( named )
    return name;
  switch ( descriptor.kind )
  {
    case kGoKindPointer:
    {
      const std::string elem = go_display_type_name(layer, descriptor.elem_addr, depth + 1);
      return elem.empty() ? "" : "*" + elem;
    }
    case kGoKindSlice:
    {
      const std::string elem = go_display_type_name(layer, descriptor.elem_addr, depth + 1);
      return elem.empty() ? "" : "[]" + elem;
    }
    case kGoKindArray:
    {
      const std::string elem = go_display_type_name(layer, descriptor.elem_addr, depth + 1);
      return elem.empty() ? "" : "[" + std::to_string(descriptor.array_len) + "]" + elem;
    }
    case kGoKindMap:
    {
      const std::string key = go_display_type_name(layer, descriptor.elem_addr, depth + 1);
      const std::string elem = go_display_type_name(layer, descriptor.elem2_addr, depth + 1);
      return key.empty() || elem.empty() ? "" : "map[" + key + "]" + elem;
    }
    case kGoKindChan:
    {
      const std::string elem = go_display_type_name(layer, descriptor.elem_addr, depth + 1);
      return elem.empty() ? "" : "chan " + elem;
    }
    default: return name; // unnamed composite beyond this slice's scope: honest skip
  }
}

std::string go_descriptor_c_name(const go_type_descriptor_t &descriptor)
{
  const std::string name = go_descriptor_display_name(descriptor);
  if ( name == "string" )
    return "go__string";
  if ( name == "js.Value" )
    return "go__js__Value"; // the known-API spelling (go_c_name_for_known)
  if ( descriptor.kind == kGoKindInterface )
    return name.empty() ? "" : "go__iface";
  if ( descriptor.kind == kGoKindSlice )
    return "go__slice";
  if ( descriptor.kind == kGoKindStruct && !name.empty() )
  {
    // pkg.Type -> go__pkg_Type (the loader sanitizer pattern: dots and
    // slashes become single underscores under a go__ prefix).
    std::string sanitized = "go__";
    for ( char c : name )
      sanitized.push_back(c == '.' || c == '/' ? '_' : c);
    return sanitized;
  }
  return "";
}

std::string go_descriptor_type_name(const go_type_descriptor_t &descriptor)
{
  return go_descriptor_display_name(descriptor);
}

// Shortest decimal that parses back to the same double (try 15, 16
// then 17 significant digits — 17 always round-trips IEEE binary64).
namespace
{
std::string go_shortest_double(double value)
{
  char buffer[64];
  for ( int precision = 15; precision <= 17; ++precision )
  {
    std::snprintf(buffer, sizeof(buffer), "%.*g", precision, value);
    if ( std::strtod(buffer, nullptr) == value )
      return std::string(buffer);
  }
  return std::string(buffer);
}
} // namespace

bool go_string_literal_text(const std::uint8_t *bytes, std::size_t len, std::string *out)
{
  if ( out == nullptr || bytes == nullptr || len == 0 )
    return false;
  // UTF-8 validation (Go strings are UTF-8; invalid sequences keep the
  // numeric display — a mojibake literal would lie about the data).
  std::size_t i = 0;
  while ( i < len )
  {
    const std::uint8_t byte = bytes[i];
    if ( byte == 0 )
      return false; // NUL cannot ride in a C string literal
    int continuations = 0;
    if ( byte < 0x80 )
      continuations = 0;
    else if ( (byte & 0xE0) == 0xC0 && byte >= 0xC2 )
      continuations = 1;
    else if ( (byte & 0xF0) == 0xE0 )
      continuations = 2;
    else if ( (byte & 0xF8) == 0xF0 && byte <= 0xF4 )
      continuations = 3;
    else
      return false;
    if ( i + std::size_t(continuations) >= len && continuations != 0 )
      return false; // truncated multi-byte sequence
    for ( int c = 1; c <= continuations; ++c )
      if ( (bytes[i + std::size_t(c)] & 0xC0) != 0x80 )
        return false;
    i += std::size_t(continuations) + 1;
  }
  out->assign(reinterpret_cast<const char *>(bytes), len);
  return true;
}

bool go_boxed_scalar_text(std::uint8_t kind, const std::uint8_t *data, std::string *out)
{
  if ( out == nullptr || data == nullptr )
    return false;
  const auto read_u64 = [&]() -> std::uint64_t
  {
    std::uint64_t value = 0;
    for ( unsigned b = 0; b < 8; ++b )
      value |= std::uint64_t(data[b]) << (b * 8);
    return value;
  };
  if ( kind == kGoKindBool )
  {
    *out = data[0] != 0 ? "true" : "false";
    return true;
  }
  if ( kind >= 2 && kind <= 6 ) // Int..Int64: signed
  {
    *out = std::to_string(std::int64_t(read_u64()));
    return true;
  }
  if ( kind >= 7 && kind <= 12 ) // Uint..Uintptr: unsigned
  {
    *out = std::to_string(read_u64());
    return true;
  }
  if ( kind == kGoKindFloat32 )
  {
    const std::uint32_t bits = std::uint32_t(data[0]) | std::uint32_t(data[1]) << 8
      | std::uint32_t(data[2]) << 16 | std::uint32_t(data[3]) << 24;
    float value = 0;
    std::memcpy(&value, &bits, sizeof(value));
    char buffer[48];
    std::snprintf(buffer, sizeof(buffer), "%.9g", double(value));
    *out = buffer;
    return true;
  }
  if ( kind == kGoKindFloat64 )
  {
    double value = 0;
    const std::uint64_t bits = read_u64();
    std::memcpy(&value, &bits, sizeof(value));
    *out = go_shortest_double(value);
    return true;
  }
  return false; // string/composite/pointer kinds: no plain scalar literal
}

bool go_member_covering(const go_type_info_t &layer, const go_type_descriptor_t &descriptor,
                        std::uint64_t offset, std::uint64_t size, std::string *name)
{
  if ( name == nullptr || size == 0 )
    return false;
  for ( const go_type_field_t &field : descriptor.fields )
  {
    if ( field.offset > offset )
      continue;
    const go_type_descriptor_t *field_type = layer.descriptor_by_addr(field.type_addr);
    if ( field_type == nullptr )
      continue; // gap-cut field width: cannot prove containment
    if ( offset + size <= field.offset + field_type->size )
    {
      *name = field.name;
      return true;
    }
  }
  return false;
}

bool go_descriptor_struct_fields(const go_type_info_t &layer,
                                 const go_type_descriptor_t &descriptor,
                                 std::vector<go_c_field_t> *fields, std::uint64_t *size)
{
  if ( fields == nullptr || size == nullptr || descriptor.kind != kGoKindStruct
       || descriptor.fields.empty() || descriptor.size == 0 )
    return false;
  fields->clear();
  *size = descriptor.size;
  // Same two-pass walk as go_c_struct_for_param: raw (offset, type_addr,
  // name) first, widths second — a gap-cut field TYPE must not kill the
  // layout when the offset delta proves its width.
  struct raw_field_t
  {
    std::string name;
    std::uint64_t offset;
    std::uint64_t type_addr;
  };
  std::vector<raw_field_t> raw;
  raw.reserve(descriptor.fields.size());
  for ( const go_type_field_t &field : descriptor.fields )
    raw.push_back({ field.name, field.offset, field.type_addr });
  std::sort(raw.begin(), raw.end(),
            [](const raw_field_t &a, const raw_field_t &b) { return a.offset < b.offset; });
  std::uint64_t cursor = 0;
  for ( size_t k = 0; k < raw.size(); ++k )
  {
    const raw_field_t &field = raw[k];
    const std::uint64_t next = k + 1 < raw.size() ? raw[k + 1].offset : descriptor.size;
    if ( field.offset < cursor )
    {
      fields->clear();
      return false; // overlapping fields: refuse to guess
    }
    if ( field.offset > cursor )
      fields->push_back(
        { "pad" + std::to_string(cursor), "__int64", cursor, field.offset - cursor });
    const go_type_descriptor_t *field_type = layer.descriptor_by_addr(field.type_addr);
    const std::uint64_t width
      = field_type != nullptr ? field_type->size : (next > field.offset ? next - field.offset : 0);
    if ( width == 0 )
      continue; // 0-size field ([0]func()): occupies nothing
    go_c_field_t rendered;
    rendered.name
      = !field.name.empty() ? go_sanitize_field(field.name) : "f" + std::to_string(field.offset);
    if ( field_type != nullptr )
    {
      const std::string field_c = go_descriptor_c_name(*field_type);
      if ( !field_c.empty() )
        rendered.c_type = "struct " + field_c;
      else if ( field_type->kind == 14 ) // float64
        rendered.c_type = "double";
      else if ( field_type->kind == 13 ) // float32
        rendered.c_type = "float";
      else if ( field_type->size == 8 )
        rendered.c_type = "__int64";
      else if ( field_type->size == 4 )
        rendered.c_type = "int";
      else if ( field_type->size == 1 )
        rendered.c_type = "bool";
      else
        rendered.c_type = "__int64"; // word-sized aggregate: one word
    }
    else
      rendered.c_type = "__int64"; // width proven by the offset delta only
    rendered.offset = field.offset;
    rendered.size = width;
    fields->push_back(std::move(rendered));
    cursor = field.offset + width;
  }
  if ( cursor != descriptor.size )
  {
    // The loader walk closes the tail with a pad; mirror it so both sides
    // register byte-identical layouts under the same name.
    if ( cursor < descriptor.size )
      fields->push_back(
        { "pad" + std::to_string(cursor), "__int64", cursor, descriptor.size - cursor });
    else
    {
      fields->clear();
      return false; // fields overrun the struct size: fail-closed
    }
  }
  return true;
}

// Blob 't' v1 codec — u32 word stream:
// [version][flags][moduledata][types][gofunc][region fields][anchor]
// [descriptor count][descriptors...][arginfo count][arginfo...]
// flags: bit0 present, bit1 layout 27 (else 26).
namespace
{

void go_blob_push_string(std::vector<uint32> *out, const std::string &text)
{
  out->push_back(uint32(text.size()));
  uint32 word = 0;
  int filled = 0;
  for ( size_t i = 0; i < text.size(); ++i )
  {
    word |= uint32(uint8(text[i])) << (filled * 8);
    if ( ++filled == 4 )
    {
      out->push_back(word);
      word = 0;
      filled = 0;
    }
  }
  if ( filled != 0 )
    out->push_back(word);
}

bool go_blob_read_string(const uint32 *&words, const uint32 *end, std::string *out)
{
  out->clear();
  if ( words == end )
    return false;
  const uint32 len = *words++;
  if ( len > 512 )
    return false;
  size_t done = 0;
  while ( done < len )
  {
    if ( words == end )
      return false;
    const uint32 word = *words++;
    for ( int b = 0; b < 4 && done < len; ++b, ++done )
      out->push_back(char((word >> (b * 8)) & 0xff));
  }
  return true;
}

} // namespace

void encode_go_type_info_blob(const go_type_info_t &info, std::vector<uint32> *out)
{
  out->clear();
  out->push_back(go_types_blob_version);
  out->push_back(uint32(info.present ? 1 : 0) | uint32(info.layout_version == 27 ? 2 : 0));
  out->push_back(uint32(uint64(info.moduledata)));
  out->push_back(uint32(uint64(info.types)));
  out->push_back(uint32(uint64(info.gofunc)));
  out->push_back(uint32(uint64(info.typedesclen)));
  out->push_back(uint32(uint64(info.etypes)));
  out->push_back(uint32(uint64(info.itaboffset)));
  out->push_back(uint32(uint64(info.itabsize)));
  out->push_back(info.typelinks_len);
  out->push_back(info.itablinks_len);
  // anchor
  out->push_back(uint32(info.anchor.present ? 1 : 0));
  out->push_back(uint32(uint64(info.anchor.header)));
  out->push_back(uint32(info.anchor.nfunc));
  out->push_back(uint32(uint64(info.anchor.ftab_base)));
  out->push_back(uint32(info.anchor.func_addrs.size()));
  for ( uint64 addr : info.anchor.func_addrs )
    out->push_back(uint32(addr));
  // descriptors —: frozen at the scan output (blob_descriptor_cap) so
  // the wire keeps carrying exactly the pre- set (the lazy resolver's
  // tier-(a) appends stay loader-side; their names ride the layout tail).
  const std::size_t descriptor_count
    = info.blob_descriptor_cap != 0 && info.blob_descriptor_cap <= info.descriptors.size()
    ? info.blob_descriptor_cap
    : info.descriptors.size();
  out->push_back(uint32(descriptor_count));
  for ( std::size_t d = 0; d < descriptor_count; ++d )
  {
    const go_type_descriptor_t &descriptor = info.descriptors[d];
    out->push_back(uint32(uint64(descriptor.addr)));
    out->push_back(uint32(descriptor.kind) | (uint32(descriptor.tflag) << 8)
                   | (uint32(uint8(descriptor.variadic ? 1 : 0)) << 16));
    out->push_back(uint32(uint64(descriptor.size)));
    out->push_back(uint32(descriptor.str_off));
    out->push_back(uint32(uint64(descriptor.ptr_to_this)));
    out->push_back(uint32(uint64(descriptor.elem_addr)));
    out->push_back(uint32(uint64(descriptor.elem2_addr)));
    out->push_back(uint32(uint64(descriptor.array_len)));
    out->push_back(uint32(uint64(descriptor.fields_addr)));
    out->push_back(uint32(descriptor.in_count) | (uint32(descriptor.out_count) << 16));
    out->push_back(uint32(descriptor.method_count));
    out->push_back(uint32(descriptor.param_addrs.size()));
    for ( uint64 addr : descriptor.param_addrs )
      out->push_back(uint32(addr));
    out->push_back(uint32(descriptor.fields.size()));
    for ( const go_type_field_t &field : descriptor.fields )
    {
      out->push_back(uint32(uint64(field.type_addr)));
      out->push_back(uint32(uint64(field.offset)));
      go_blob_push_string(out, field.name);
    }
    go_blob_push_string(out, descriptor.name);
    go_blob_push_string(out, descriptor.pkg_path);
  }
  // arginfo
  out->push_back(uint32(info.arginfo.size()));
  for ( const go_arginfo_t &arginfo : info.arginfo )
  {
    out->push_back(uint32(arginfo.present ? 1 : 0));
    out->push_back(uint32(arginfo.args_bytes));
    out->push_back(uint32(arginfo.slots.size()));
    for ( size_t k = 0; k < arginfo.slots.size(); ++k )
      out->push_back(
        arginfo.slots[k].offset | (arginfo.slots[k].size << 8)
        | (uint32(arginfo.slots[k].depth) << 16)
        | (uint32(k < arginfo.group_starts.size() && arginfo.group_starts[k] != 0 ? 1 : 0) << 24));
  }
  // Layout tail (version 2): one record per defined body, the single
  // source the prototype path and the call-site path both read. Fields ride
  // only for named struct groups (the emit-side til registration payload);
  // results ride only for tier (a) (the gated GOFR display stays authoritative
  // for every other result view).
  out->push_back(uint32(info.layouts.size()));
  for ( const go_func_layout_t &layout : info.layouts )
  {
    out->push_back(uint32(layout.source) | (uint32(layout.variadic ? 1 : 0) << 8));
    out->push_back(uint32(layout.params.size()));
    for ( const go_layout_param_t &param : layout.params )
    {
      out->push_back(uint32(param.is_group ? 1 : 0) | (uint32(param.type) << 1));
      out->push_back(param.offset);
      out->push_back(param.bytes);
      go_blob_push_string(out, param.c_name);
      out->push_back(uint32(param.fields.size()));
      for ( const go_c_field_t &field : param.fields )
      {
        out->push_back(uint32(field.offset));
        out->push_back(uint32(field.size));
        go_blob_push_string(out, field.name);
        go_blob_push_string(out, field.c_type);
      }
    }
    out->push_back(uint32(layout.results.size()));
    for ( const go_layout_param_t &result : layout.results )
    {
      out->push_back(uint32(result.is_group ? 1 : 0) | (uint32(result.type) << 1));
      out->push_back(result.offset);
      out->push_back(result.bytes);
      go_blob_push_string(out, result.c_name);
      out->push_back(0); // no result fields on the wire
    }
    go_blob_push_string(out, layout.anchor);
  }
}

bool decode_go_type_info_blob(const uint32 *words, std::size_t word_count, go_type_info_t *out)
{
  if ( out == nullptr || words == nullptr || word_count < 15 )
    return false;
  const uint32 *cursor = words;
  const uint32 *end = words + word_count;
  const uint32 version = *cursor++;
  if ( version != go_types_blob_version && version != go_types_blob_v1 )
    return false; // unknown version: fail closed
  const uint32 flags = *cursor++;
  *out = go_type_info_t();
  out->present = (flags & 1) != 0;
  out->layout_version = (flags & 2) != 0 ? 27 : 26;
  out->moduledata = *cursor++;
  out->types = *cursor++;
  out->gofunc = *cursor++;
  out->typedesclen = *cursor++;
  out->etypes = *cursor++;
  out->itaboffset = *cursor++;
  out->itabsize = *cursor++;
  out->typelinks_len = *cursor++;
  out->itablinks_len = *cursor++;
  out->anchor.present = *cursor++ != 0;
  out->anchor.header = *cursor++;
  out->anchor.nfunc = *cursor++;
  out->anchor.ftab_base = *cursor++;
  const uint32 func_count = *cursor++;
  if ( func_count > (1u << 20) )
    return false;
  out->anchor.func_addrs.resize(func_count);
  for ( uint32 i = 0; i < func_count; ++i )
  {
    if ( cursor == end )
      return false;
    out->anchor.func_addrs[i] = *cursor++;
  }
  if ( cursor == end )
    return false;
  const uint32 descriptor_count = *cursor++;
  if ( descriptor_count > (1u << 20) )
    return false;
  out->descriptors.reserve(descriptor_count);
  for ( uint32 i = 0; i < descriptor_count; ++i )
  {
    if ( end - cursor < 12 )
      return false;
    go_type_descriptor_t descriptor;
    descriptor.addr = *cursor++;
    const uint32 packed = *cursor++;
    descriptor.kind = uint8(packed & 0xff);
    descriptor.tflag = uint8((packed >> 8) & 0xff);
    descriptor.variadic = ((packed >> 16) & 1) != 0;
    descriptor.size = *cursor++;
    descriptor.str_off = int32(*cursor++);
    descriptor.ptr_to_this = *cursor++;
    descriptor.elem_addr = *cursor++;
    descriptor.elem2_addr = *cursor++;
    descriptor.array_len = *cursor++;
    descriptor.fields_addr = *cursor++;
    const uint32 counts = *cursor++;
    descriptor.in_count = uint16(counts & 0xffff);
    descriptor.out_count = uint16(counts >> 16);
    descriptor.method_count = uint16(*cursor++);
    const uint32 param_count = *cursor++;
    if ( param_count > 1024 || end - cursor < int(param_count) )
      return false;
    descriptor.param_addrs.resize(param_count);
    for ( uint32 k = 0; k < param_count; ++k )
      descriptor.param_addrs[k] = *cursor++;
    const uint32 field_count = *cursor++;
    if ( field_count > 8192 )
      return false;
    descriptor.fields.resize(field_count);
    for ( uint32 k = 0; k < field_count; ++k )
    {
      if ( end - cursor < 2 )
        return false;
      descriptor.fields[k].type_addr = *cursor++;
      descriptor.fields[k].offset = *cursor++;
      if ( !go_blob_read_string(cursor, end, &descriptor.fields[k].name) )
        return false;
    }
    if ( !go_blob_read_string(cursor, end, &descriptor.name) )
      return false;
    if ( !go_blob_read_string(cursor, end, &descriptor.pkg_path) )
      return false;
    out->descriptors.push_back(std::move(descriptor));
  }
  if ( cursor == end )
    return false;
  const uint32 arginfo_count = *cursor++;
  if ( arginfo_count > (1u << 20) )
    return false;
  out->arginfo.resize(arginfo_count);
  for ( uint32 i = 0; i < arginfo_count; ++i )
  {
    if ( end - cursor < 3 )
      return false;
    out->arginfo[i].present = *cursor++ != 0;
    out->arginfo[i].args_bytes = int32(*cursor++);
    const uint32 slot_count = *cursor++;
    if ( slot_count > 1024 || end - cursor < int(slot_count) )
      return false;
    out->arginfo[i].slots.resize(slot_count);
    out->arginfo[i].group_starts.resize(slot_count);
    for ( uint32 k = 0; k < slot_count; ++k )
    {
      const uint32 packed = *cursor++;
      out->arginfo[i].slots[k].offset = packed & 0xff;
      out->arginfo[i].slots[k].size = (packed >> 8) & 0xff;
      out->arginfo[i].slots[k].depth = uint8((packed >> 16) & 0xff);
      out->arginfo[i].group_starts[k] = uint8((packed >> 24) & 0xff);
    }
  }
  // Layout tail (version 2 only): a v1 stream ends here — cursor == end
  // — with an empty layout vector (canonical extent names, no agreement).
  if ( version == go_types_blob_v1 )
    return cursor == end;
  if ( cursor == end )
    return false;
  const uint32 layout_count = *cursor++;
  if ( layout_count > (1u << 20) )
    return false;
  out->layouts.resize(layout_count);
  for ( uint32 i = 0; i < layout_count; ++i )
  {
    go_func_layout_t &layout = out->layouts[i];
    if ( cursor == end )
      return false;
    const uint32 packed = *cursor++;
    layout.source = go_layout_source_t(uint8(packed & 0xff));
    if ( layout.source != go_layout_source_t::kNone
         && layout.source != go_layout_source_t::kDescriptor
         && layout.source != go_layout_source_t::kArgInfo
         && layout.source != go_layout_source_t::kEvidence )
      return false; // unknown confidence tier: fail closed
    layout.variadic = ((packed >> 8) & 1) != 0;
    if ( cursor == end )
      return false;
    const uint32 param_count = *cursor++;
    if ( param_count > 1024 )
      return false;
    layout.params.resize(param_count);
    for ( uint32 p = 0; p < param_count; ++p )
    {
      go_layout_param_t &param = layout.params[p];
      if ( end - cursor < 3 )
        return false;
      const uint32 head = *cursor++;
      param.is_group = (head & 1) != 0;
      param.type = uint8((head >> 1) & 0xff);
      param.offset = *cursor++;
      param.bytes = *cursor++;
      if ( !go_blob_read_string(cursor, end, &param.c_name) )
        return false;
      if ( cursor == end )
        return false;
      const uint32 field_count = *cursor++;
      if ( field_count > 1024 || end - cursor < int(field_count) )
        return false;
      param.fields.resize(field_count);
      for ( uint32 f = 0; f < field_count; ++f )
      {
        if ( end - cursor < 2 )
          return false;
        param.fields[f].offset = *cursor++;
        param.fields[f].size = *cursor++;
        if ( !go_blob_read_string(cursor, end, &param.fields[f].name) )
          return false;
        if ( !go_blob_read_string(cursor, end, &param.fields[f].c_type) )
          return false;
      }
    }
    if ( cursor == end )
      return false;
    const uint32 result_count = *cursor++;
    if ( result_count > 64 )
      return false;
    layout.results.resize(result_count);
    for ( uint32 r = 0; r < result_count; ++r )
    {
      go_layout_param_t &result = layout.results[r];
      if ( end - cursor < 3 )
        return false;
      const uint32 head = *cursor++;
      result.is_group = (head & 1) != 0;
      result.type = uint8((head >> 1) & 0xff);
      result.offset = *cursor++;
      result.bytes = *cursor++;
      if ( !go_blob_read_string(cursor, end, &result.c_name) )
        return false;
      if ( cursor == end )
        return false;
      const uint32 field_count = *cursor++;
      if ( field_count != 0 )
        return false; // result fields never ride the wire
    }
    if ( !go_blob_read_string(cursor, end, &layout.anchor) )
      return false;
  }
  return cursor == end;
}

// The display name this body carries (name section first, then pclntab) —
// the same priority the loader's function_name() applies, minus the dedup.
std::string go_body_display_name(const module_t &module, std::size_t body_index)
{
  const uint32 fn_index = uint32(module.imported_func_types.size() + body_index);
  for ( const export_t &entry : module.function_names )
    if ( entry.index == fn_index )
      return entry.name;
  if ( body_index < module.pclntab_function_names.size() )
    return module.pclntab_function_names[body_index];
  return std::string();
}

// The known-API js.Value callee gate for the global
// renaming evidence walk (go_jsvalue_receiver_globals, ir.cpp). Only
// defined bodies carry the table's display names; imports fail closed.
// The comparison normalizes the body display name the way the loader's
// naming pipeline does for these symbols — invalid identifier bytes
// become '_' but dots stay (the kGoKnownApi keys are exactly that form,
// e.g. "syscall_js.Value.Call").
// kGoKnownApi lives in the anonymous namespace above (same TU, visible).
bool go_known_api_jsvalue_callee(const module_t &module, std::uint32_t fn_index)
{
  if ( module.imported_func_types.empty() && fn_index != 0 )
    return false;
  if ( fn_index < module.imported_func_types.size() )
    return false;
  const std::size_t body_index = std::size_t(fn_index - module.imported_func_types.size());
  if ( body_index >= module.bodies.size() )
    return false;
  std::string name;
  for ( const char c : go_body_display_name(module, body_index) )
  {
    const bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')
      || c == '_' || c == '.';
    name.push_back(ok ? c : '_');
  }
  if ( name.empty() )
    return false;
  for ( const known_api_entry_t &known : kGoKnownApi )
    if ( known.recv != nullptr && std::string(known.recv) == "js.Value" && name == known.name )
      return true;
  return false;
}

} // namespace wasmhex::loader
