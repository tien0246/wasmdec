// Recover Go function names from a pclntab embedded in the data section.
// Supported wire formats: go1.20+ (magic 0xFFFFFFF1) and go1.18/1.19
// (0xFFFFFFF0); names are NUL-terminated strings in funcnametab.
#include "../types.hpp"

#include <algorithm>
#include <cstring>

namespace wasmhex::loader
{

namespace
{

struct pclntab_span_t
{
  int64 dest;
  const uchar *data;
  size_t size;
};

const uchar *pclntab_mem_read(const std::vector<pclntab_span_t> &spans, int64 addr, size_t bytes)
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
  const pclntab_span_t &span = spans[lo - 1];
  const int64 offset = addr - span.dest;
  if ( offset < 0 || offset + int64(bytes) > int64(span.size) )
    return nullptr; // crossing a memory gap: not backed by static data
  return span.data + size_t(offset);
}

// Go splits the pclntab across many small data segments, so a multi-byte
// field can straddle two segments, and pieces of the pclntab can even sit
// on memory that no active segment initializes (zero fill, like BSS):
// read byte-wise through the span lookup and treat uncovered bytes as 0.
uint64_t pclntab_read_uint(const std::vector<pclntab_span_t> &spans, int64 addr, size_t width)
{
  uint64_t value = 0;
  for ( size_t i = 0; i < width; ++i )
  {
    const uchar *ptr = pclntab_mem_read(spans, addr + int64(i), 1);
    value |= uint64_t(ptr != nullptr ? *ptr : 0) << (i * 8);
  }
  return value;
}

bool pclntab_read_string(const std::vector<pclntab_span_t> &spans, int64 addr, std::string *out)
{
  out->clear();
  for ( size_t length = 0; length < 256; ++length )
  {
    const uchar *ptr = pclntab_mem_read(spans, addr + int64(length), 1);
    // Name characters must come from loaded bytes; the terminating NUL may
    // sit in the zero fill right past a segment boundary (Go cuts the
    // funcnametab into per-symbol segments, sometimes mid-string).
    const uchar c = ptr != nullptr ? *ptr : 0;
    if ( c == 0 )
      return !out->empty();
    if ( c < 0x20 || c > 0x7E )
      return false;
    out->push_back(char(c));
  }
  return false;
}

} // namespace

bool module_t::parse_pclntab()
{
  pclntab_function_names.clear();
  go_types = go_type_info_t(); // Rescan starts from a clean layer
  std::vector<pclntab_span_t> spans;
  for ( size_t i = 0; i < data_segment_offsets.size(); ++i )
  {
    if ( data_segment_offsets[i] < 0 || data_segment_sizes[i] == 0 )
      continue; // passive segments are not part of the static memory image
    spans.push_back(
      { data_segment_offsets[i], data_segment_data[i].data(), size_t(data_segment_sizes[i]) });
  }
  if ( spans.empty() || defined_func_types.empty() )
    return true;
  std::sort(spans.begin(), spans.end(), [](const pclntab_span_t &left, const pclntab_span_t &right)
            { return left.dest < right.dest; });

  auto try_header = [&](int64 header) -> bool
  {
    const uchar *bytes = pclntab_mem_read(spans, header, 8);
    if ( bytes == nullptr )
      return false;
    uint32_t magic = 0;
    for ( size_t i = 0; i < 4; ++i )
      magic |= uint32_t(bytes[i]) << (i * 8);
    // Layer 3: the pclntab wire format splits the eras — 0xFFFFFFF1 is
    // go1.20+, 0xFFFFFFF0 the go1.18/1.19 form.
    if ( magic == 0xFFFFFFF1 && go_profile.pclntab_wire.empty() )
      go_profile.pclntab_wire = "F1";
    else if ( magic == 0xFFFFFFF0 && go_profile.pclntab_wire.empty() )
      go_profile.pclntab_wire = "F0";
    if ( (magic != 0xFFFFFFF1 && magic != 0xFFFFFFF0) || bytes[4] != 0 || bytes[5] != 0 )
      return false;
    const size_t ptr_size = bytes[7];
    const uchar min_lc = bytes[6];
    if ( ptr_size != 4 && ptr_size != 8 )
      return false;
    if ( min_lc != 1 && min_lc != 2 && min_lc != 4 )
      return false;
    // go1.20+ keeps an unused uintptr (the old textStart) between nfiles
    // and funcnameOffset; go1.18/1.19 does not.
    const int64 fields = header + 8 + int64(ptr_size) * (magic == 0xFFFFFFF1 ? 3 : 2);
    const uint64_t nfunc = pclntab_read_uint(spans, header + 8, ptr_size);
    if ( nfunc == 0 || nfunc > defined_func_types.size() + 1 )
    {
      return false;
    }
    const int64 funcname_off = int64(pclntab_read_uint(spans, fields, ptr_size));
    const int64 pcln_off = int64(pclntab_read_uint(spans, fields + 4 * int64(ptr_size), ptr_size));
    if ( funcname_off <= 0 || pcln_off <= 0 )
    {
      return false;
    }
    const int64 functab = header + pcln_off;
    uint64_t first_entry = UINT64_MAX;
    uint64_t previous = 0;
    std::vector<std::string> names(defined_func_types.size());
    size_t recovered = 0;
    for ( uint64_t i = 0; i < nfunc; ++i )
    {
      const uint64_t entry_off = pclntab_read_uint(spans, functab + int64(i) * 8 + 0, 4);
      const uint64_t func_off = pclntab_read_uint(spans, functab + int64(i) * 8 + 4, 4);
      if ( entry_off > UINT32_MAX || func_off > UINT32_MAX || entry_off < previous )
      {
        return false; // functab must be sorted by entryoff
      }
      previous = entry_off;
      if ( first_entry == UINT64_MAX )
        first_entry = entry_off;
      const uint64_t index = entry_off - first_entry;
      if ( index >= names.size() )
        return false;
      const int64_t name_off = int64_t(pclntab_read_uint(spans, functab + int64(func_off) + 4, 4));
      if ( name_off < 0 ) // 0 is valid: entry 0 names "go:buildid"
        return false;
      std::string name;
      if ( !pclntab_read_string(spans, header + funcname_off + name_off, &name) )
        return false;
      if ( names[index].empty() )
      {
        names[index] = name;
        ++recovered;
      }
    }
    if ( recovered + 1 < names.size() )
    {
      return false; // a real pclntab covers (nearly) every function
    }
    pclntab_function_names = std::move(names);
    return true;
  };

  for ( const pclntab_span_t &span : spans )
  {
    for ( size_t p = 0; p + 8 <= span.size; ++p )
    {
      if ( span.data[p] != 0xF1 && span.data[p] != 0xF0 )
        continue;
      if ( span.data[p + 1] != 0xFF || span.data[p + 2] != 0xFF || span.data[p + 3] != 0xFF )
        continue;
      if ( try_header(span.dest + int64(p)) )
      {
        // Name recovery succeeded — retain the functab shape and run
        // the moduledata/type scan before stopping (additive; try_header
        // itself is unchanged).
        retain_go_pclntab();
        scan_go_types(*this);
        return true;
      }
    }
  }
  // No header recovered names (a fully stripped module, or the
  // go1.26.5 long-`type:.eq`-name cap) — a shape-valid header still anchors
  // the type data layer; strip modules keep it empty here.
  retain_go_pclntab();
  scan_go_types(*this);
  return true;
}

// Addition (below this line): retain the validated pclntab shape for
// the Go type data layer. The name recovery above is untouched; a module
// whose names fail (go1.26.5 emits `type:.eq.*` pclntab names longer than
// the 256-byte name cap, which blanks pclntab_function_names on every
// go1.26.5 build) still anchors the moduledata scan here.
namespace
{

// Validate the pcHeader shape (magic/ptrSize/minLC/nfunc + positive section
// offsets, field layout) WITHOUT decoding any names, then retain the
// per-defined-function _func addresses using the same entryoff-delta rule
// as the name recovery.
bool retain_go_functab(module_t &module, const std::vector<pclntab_span_t> &spans, int64 header)
{
  const uchar *bytes = pclntab_mem_read(spans, header, 8);
  if ( bytes == nullptr )
    return false;
  uint32_t magic = 0;
  for ( size_t i = 0; i < 4; ++i )
    magic |= uint32_t(bytes[i]) << (i * 8);
  if ( (magic != 0xFFFFFFF1 && magic != 0xFFFFFFF0) || bytes[4] != 0 || bytes[5] != 0 )
    return false;
  const size_t ptr_size = bytes[7];
  if ( ptr_size != 4 && ptr_size != 8 )
    return false;
  if ( bytes[6] != 1 && bytes[6] != 2 && bytes[6] != 4 )
    return false;
  const uint64_t nfunc = pclntab_read_uint(spans, header + 8, ptr_size);
  if ( nfunc == 0 || nfunc > module.defined_func_types.size() + 1 )
    return false;
  const int64 fields = header + 8 + int64(ptr_size) * (magic == 0xFFFFFFF1 ? 3 : 2);
  const int64 funcname_off = int64(pclntab_read_uint(spans, fields, ptr_size));
  const int64 cutab_off = int64(pclntab_read_uint(spans, fields + ptr_size, ptr_size));
  const int64 pcln_off = int64(pclntab_read_uint(spans, fields + 4 * int64(ptr_size), ptr_size));
  if ( funcname_off <= 0 || cutab_off <= 0 || pcln_off <= 0 )
    return false;
  const int64 functab = header + pcln_off;

  go_pclntab_anchor_t anchor;
  anchor.present = true;
  anchor.header = uint64(header);
  anchor.nfunc = nfunc;
  anchor.funcname_off = funcname_off;
  anchor.cutab_off = cutab_off;
  anchor.pcln_off = pcln_off;
  anchor.ftab_base = functab;
  anchor.func_addrs.assign(module.defined_func_types.size(), 0);

  uint64_t first_entry = UINT64_MAX;
  uint64_t previous = 0;
  size_t mapped = 0;
  for ( uint64_t i = 0; i < nfunc; ++i )
  {
    const uint64_t entry_off = pclntab_read_uint(spans, functab + int64(i) * 8 + 0, 4);
    const uint64_t func_off = pclntab_read_uint(spans, functab + int64(i) * 8 + 4, 4);
    if ( entry_off > UINT32_MAX || func_off > UINT32_MAX || entry_off < previous )
      return false; // functab must be sorted by entryoff
    previous = entry_off;
    if ( first_entry == UINT64_MAX )
      first_entry = entry_off;
    const uint64_t index = entry_off - first_entry;
    if ( index >= anchor.func_addrs.size() )
      return false;
    if ( anchor.func_addrs[size_t(index)] == 0 )
    {
      anchor.func_addrs[size_t(index)] = uint64(functab + int64(func_off));
      ++mapped;
    }
  }
  if ( mapped + 1 < anchor.func_addrs.size() )
    return false; // a real functab covers (nearly) every function
  module.go_types.anchor = std::move(anchor);
  return true;
}

} // namespace

void module_t::retain_go_pclntab()
{
  std::vector<pclntab_span_t> spans;
  for ( size_t i = 0; i < data_segment_offsets.size(); ++i )
  {
    if ( data_segment_offsets[i] < 0 || data_segment_sizes[i] == 0 )
      continue;
    spans.push_back(
      { data_segment_offsets[i], data_segment_data[i].data(), size_t(data_segment_sizes[i]) });
  }
  if ( spans.empty() || defined_func_types.empty() )
    return;
  std::sort(spans.begin(), spans.end(), [](const pclntab_span_t &left, const pclntab_span_t &right)
            { return left.dest < right.dest; });
  // First shape-valid header wins — the same candidate order the name
  // recovery above uses, so both layers agree on the header.
  for ( const pclntab_span_t &span : spans )
  {
    for ( size_t p = 0; p + 8 <= span.size; ++p )
    {
      if ( span.data[p] != 0xF1 && span.data[p] != 0xF0 )
        continue;
      if ( span.data[p + 1] != 0xFF || span.data[p + 2] != 0xFF || span.data[p + 3] != 0xFF )
        continue;
      if ( retain_go_functab(*this, spans, span.dest + int64(p)) )
        return;
    }
  }
}

} // namespace wasmhex::loader
