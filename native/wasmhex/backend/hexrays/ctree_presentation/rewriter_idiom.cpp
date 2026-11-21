#include <cstddef>
#include <cstring>

namespace wasmhex
{
namespace idiom_detail
{
inline bool is_ident_char(char c)
{
  return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_'
    || c == '$';
}

// Strip a trailing `_[0-9]+` collision suffix (added by
// used_global_names when multiple GOT globals name the same symbol).
inline bool strip_collision_suffix_len(const char *mangled, size_t *out_len)
{
  if ( mangled == nullptr || out_len == nullptr )
    return false;
  const size_t len = strlen(mangled);
  size_t pos = len;
  while ( pos > 2 && mangled[pos - 1] >= '0' && mangled[pos - 1] <= '9' )
    --pos;
  if ( pos == len || pos <= 2 || mangled[pos - 1] != '_' )
    return false;
  *out_len = pos - 1;
  return true;
}

// Advance `i` past an `operator...` token so `<`, `>`, `(`, `)`, `[`, `]`
// that form the operator name itself do not perturb bracket depth counters.
inline bool skip_operator_token(const char *s, size_t limit, size_t *i)
{
  const size_t pos = *i;
  if ( (pos != 0 && is_ident_char(s[pos - 1])) || pos + 8 > limit
       || strncmp(s + pos, "operator", 8) != 0 || (pos + 8 < limit && is_ident_char(s[pos + 8])) )
    return false;
  size_t cur = pos + 8;
  while ( cur < limit && s[cur] == ' ' )
    ++cur;
  if ( cur + 1 < limit && s[cur] == '(' && s[cur + 1] == ')' )
    cur += 2;
  else if ( cur + 1 < limit && s[cur] == '[' && s[cur + 1] == ']' )
    cur += 2;
  else if ( cur + 2 < limit
            && (strncmp(s + cur, "->*", 3) == 0 || strncmp(s + cur, "<=>", 3) == 0
                || strncmp(s + cur, "<<=", 3) == 0 || strncmp(s + cur, ">>=", 3) == 0) )
    cur += 3;
  else if ( cur + 1 < limit
            && (strncmp(s + cur, "->", 2) == 0 || strncmp(s + cur, "<<", 2) == 0
                || strncmp(s + cur, ">>", 2) == 0 || strncmp(s + cur, "<=", 2) == 0
                || strncmp(s + cur, ">=", 2) == 0) )
    cur += 2;
  else if ( cur < limit && (s[cur] == '<' || s[cur] == '>') )
    ++cur;
  *i = cur;
  return true;
}

// Parse a demangled C++ signature, strip its parameter list, and split
// any leading return type from the qualified-id by scanning at depth 0
// outside <...>, (...), and [...].
inline bool split_demangled_callee_span(const char *s, size_t n, size_t *out_start, size_t *out_end)
{
  if ( s == nullptr || n == 0 || out_start == nullptr || out_end == nullptr )
    return false;

  int angle_depth = 0;
  int paren_depth = 0;
  int bracket_depth = 0;
  size_t param_paren = size_t(-1);

  for ( size_t i = 0; i < n; )
  {
    if ( skip_operator_token(s, n, &i) )
      continue;
    const char c = s[i];
    if ( c == '(' )
    {
      if ( angle_depth == 0 && bracket_depth == 0 && paren_depth == 0 )
        param_paren = i;
      ++paren_depth;
    }
    else if ( c == ')' )
    {
      if ( paren_depth > 0 )
        --paren_depth;
    }
    else if ( c == '[' )
    {
      ++bracket_depth;
    }
    else if ( c == ']' )
    {
      if ( bracket_depth > 0 )
        --bracket_depth;
    }
    else if ( c == '<' && paren_depth == 0 && bracket_depth == 0 )
    {
      ++angle_depth;
    }
    else if ( c == '>' && paren_depth == 0 && bracket_depth == 0 )
    {
      if ( angle_depth > 0 )
        --angle_depth;
    }
    ++i;
  }

  if ( param_paren == size_t(-1) || param_paren == 0 )
    return false;

  angle_depth = 0;
  paren_depth = 0;
  bracket_depth = 0;
  size_t name_start = 0;
  bool in_top_operator = false;

  for ( size_t i = 0; i < param_paren; )
  {
    const bool at_top = angle_depth == 0 && paren_depth == 0 && bracket_depth == 0;
    if ( skip_operator_token(s, param_paren, &i) )
    {
      if ( at_top )
        in_top_operator = true;
      continue;
    }
    const char c = s[i];
    if ( c == '(' )
    {
      ++paren_depth;
    }
    else if ( c == ')' )
    {
      if ( paren_depth > 0 )
        --paren_depth;
    }
    else if ( c == '[' )
    {
      ++bracket_depth;
    }
    else if ( c == ']' )
    {
      if ( bracket_depth > 0 )
        --bracket_depth;
    }
    else if ( c == '<' && paren_depth == 0 && bracket_depth == 0 )
    {
      ++angle_depth;
    }
    else if ( c == '>' && paren_depth == 0 && bracket_depth == 0 )
    {
      if ( angle_depth > 0 )
        --angle_depth;
    }
    else if ( angle_depth == 0 && paren_depth == 0 && bracket_depth == 0 && !in_top_operator )
    {
      if ( c == ' ' || c == '\t' || c == '*' || c == '&' )
        name_start = i + 1;
    }
    ++i;
  }

  while ( name_start < param_paren && s[name_start] == ' ' )
    ++name_start;
  size_t name_end = param_paren;
  while ( name_end > name_start && s[name_end - 1] == ' ' )
    --name_end;
  if ( name_end <= name_start || name_end - name_start <= 2 )
    return false;
  bool has_scope = false;
  for ( size_t i = name_start; i + 1 < name_end; ++i )
  {
    if ( s[i] == ':' && s[i + 1] == ':' )
    {
      has_scope = true;
      break;
    }
  }
  if ( !has_scope )
    return false;
  *out_start = name_start;
  *out_end = name_end;
  return true;
}
} // namespace idiom_detail
} // namespace wasmhex

#ifndef WASMHEX_PURE_STRING_TEST

#include "ctree.hpp"

#include "../../../ida/loader/options.hpp"

#include "../../../backend/hexrays/register_mapping/frame.hpp"
#include <bytes.hpp>
#include <name.hpp>
#include <segment.hpp>
#include <typeinf.hpp>

#include <map>
#include <set>
#include <vector>

#include "ctree_types.hpp"

namespace wasmhex
{

extern "C" __attribute__((used)) const char wasmhex_s2_build_marker[]
  = "WASMHEX_BUILD_MARKER_M4_S2_CPP_CALL_DISPLAY";

namespace
{
bool strip_collision_suffix(const char *mangled, qstring *out)
{
  size_t len = 0;
  if ( !idiom_detail::strip_collision_suffix_len(mangled, &len) )
    return false;
  *out = qstring(mangled, len);
  return true;
}

bool split_demangled_callee(const qstring &demangled, qstring *out)
{
  size_t start = 0;
  size_t end = 0;
  if ( !idiom_detail::split_demangled_callee_span(demangled.c_str(), demangled.length(), &start,
                                                  &end) )
    return false;
  *out = demangled.substr(start, end);
  return true;
}

// Display name for an Itanium-mangled callee.
// Returns the qualified-id without leading return type or parameter list —
// "duckdb::LogicalOperator::Cast<duckdb::LogicalProjection>". If demangling
// fails because of a `_[0-9]+` collision suffix, strips the suffix and
// retries.
bool demangled_callee_display(const char *mangled, qstring *out)
{
  if ( mangled == nullptr || strncmp(mangled, "_Z", 2) != 0 )
    return false;
  qstring demangled;
  if ( demangle_name(&demangled, mangled, 0) <= 0 )
  {
    qstring stripped;
    if ( !strip_collision_suffix(mangled, &stripped)
         || demangle_name(&demangled, stripped.c_str(), 0) <= 0 )
      return false;
  }
  return split_demangled_callee(demangled, out);
}

const cexpr_t *strip_all_casts(const cexpr_t *e)
{
  while ( e != nullptr && e->op == cot_cast && e->x != nullptr )
    e = e->x;
  return e;
}

// Name of an object/helper expression (imported trampolines materialize
// either way depending on how the call site was lowered; a function used
// as a value may sit behind an address-of).
const cexpr_t *strip_value_wrappers(const cexpr_t *e)
{
  e = strip_all_casts(e);
  while ( e != nullptr && (e->op == cot_ref || e->op == cot_ptr) && e->x != nullptr )
    e = strip_all_casts(e->x);
  return e;
}

bool expression_name(const cexpr_t *e, qstring *out)
{
  e = strip_value_wrappers(e);
  if ( e == nullptr )
    return false;
  if ( e->op == cot_obj )
  {
    get_name(out, e->obj_ea);
    return true;
  }
  if ( e->op == cot_helper )
  {
    *out = e->helper;
    return true;
  }
  return false;
}

cexpr_t *clone_expr(const cexpr_t &expression)
{
  return new cexpr_t(expression);
}

// C++4: `__memory_base + K` where K lands on a printable, NUL-terminated
// string inside the wasm linear-memory segment — render the literal instead
// of the raw linear address. Fail-closed: no linear-memory segment,
// unprintable bytes or missing terminator keep the arithmetic.
bool memory_base_string(ea_t data_base, uint64 offset, qstring *out, ea_t *at_out = nullptr)
{
  if ( data_base == BADADDR || offset > uint64(UINT64_MAX - uint64(data_base)) )
    return false;
  const ea_t at = ea_t(data_base + offset);
  if ( get_segment_ea(at) == BADADDR )
    return false;
  uchar byte = 0;
  for ( size_t length = 0; length < 200; ++length )
  {
    if ( get_bytes(&byte, 1, at + length) != 1 )
      return false;
    if ( byte == 0 )
    {
      if ( length == 0 )
        return false;
      if ( at_out != nullptr )
        *at_out = at; // the literal's anchor: the data EA, never an insn EA
      return true;
    }
    if ( byte < 0x20 || byte > 0x7E )
      return false;
    out->append(char(byte));
  }
  return false;
}

cexpr_t *make_string_literal(const qstring &text)
{
  cexpr_t *literal = new cexpr_t;
  literal->op = cot_str;
  literal->string = qstrdup(text.c_str());
  literal->exflags |= EXFL_CSTR;
  tinfo_t char_type = tinfo_t(BT_INT8); // char == signed byte on wasm targets
  literal->type.create_array(char_type, asize_t(text.length() + 1));
  return literal;
}

// C++6: ICF-fold group index
// wasm-ld --icf folds functions with identical machine code into one
// body, and the surviving DWARF name then describes only ONE of the
// folded originals — `duckdb::DuckDB::~DuckDB(&v4->field_12)` reading as
// a DuckDB destructor while the body is a shared_ptr cleanup. The names
// are real, so nothing is renamed: functions are grouped by their raw
// .text bytes (identical bodies = identical bytes in wasm — indices,
// immediates and all), and members of an interesting group get a
// REPEATABLE FUNCTION COMMENT listing the other folded names. Purely
// factual: no semantics are guessed, only names are listed.
// De-emphasis: a group is interesting only when its named
// members come from at least two different top-level families
// ("duckdb::..." vs "std::..."). A group of same-family names (or of
// synthetic wasm_func_N names only) carries no new information.
class icf_index_t
{
public:
  struct group_t
  {
    qvector<ea_t> members;
    qvector<qstring> names; // parallel to members
  };

  // Group of `fn`, or nullptr when the function is unique / synthetic
  // singleton. The index is built lazily once per database (a cheap
  // signature detects a database switch in long-lived GUI processes).
  const group_t *group_of(ea_t fn_ea)
  {
    if ( !build_if_needed() )
      return nullptr;
    const auto found = group_by_member.find(fn_ea);
    return found == group_by_member.end() ? nullptr : found->second;
  }

private:
  std::map<ea_t, const group_t *> group_by_member;
  std::vector<group_t> storage;
  uint64 signature = UINT64_MAX;

  static uint64 fnv1a(const uchar *data, size_t size, uint64 seed)
  {
    uint64 hash = seed;
    for ( size_t i = 0; i < size; ++i )
    {
      hash ^= data[i];
      hash *= 0x100000001B3ULL;
    }
    return hash;
  }

  // Top-level family of a demangled name: everything before the first
  // "::". Synthetic wasm_func_N names collapse to one family (they add
  // no information to an alias list).
  static qstring family_of(const qstring &name)
  {
    if ( starts_with_wasm_func(name) )
      return qstring("wasmhex_synthetic");
    const size_t sep = name.find("::");
    if ( sep == qstring::npos )
      return name;
    return name.substr(0, sep);
  }

  static bool starts_with_wasm_func(const qstring &name)
  {
    return strncmp(name.c_str(), "wasm_func_", 10) == 0;
  }

  bool build_if_needed()
  {
    const size_t quantity = size_t(get_func_qty());
    const ea_t first = quantity != 0 ? get_func_ea_by_num(0) : BADADDR;
    const uint64 current = uint64(quantity) ^ uint64(first);
    if ( signature == current )
      return true;
    signature = current;
    group_by_member.clear();
    storage.clear();
    std::map<uint64, size_t> hash_to_slot;
    for ( size_t i = 0; i < quantity; ++i )
    {
      func_entry_info_t fn;
      if ( !get_func_entry_info_by_num(&fn, i) )
        continue;
      const asize_t size = fn.end_ea - fn.start_ea;
      if ( size < 8 )
        continue; // trivial bodies (i32.const/end) are identical everywhere
      bytevec_t bytes;
      bytes.resize_noinit(size);
      const ssize_t got = get_bytes(bytes.begin(), ssize_t(size), fn.start_ea);
      const bool probing = options::debug("cpp"); // Env reads live in loader/options.cpp
      if ( probing )
        msg("WASMHEX_CPP_PROBE icf fn %u ea=%a size=%u got=%d\n", unsigned(i), fn.start_ea,
            unsigned(size), int(got));
      if ( got != ssize_t(size) )
        continue;
      const uint64 hash = fnv1a(bytes.begin(), size, 0xCBF29CE484222325ULL);
      qstring name;
      get_name(&name, fn.start_ea);
      if ( name.empty() )
        continue;
      const auto found = hash_to_slot.find(hash);
      if ( found == hash_to_slot.end() )
      {
        hash_to_slot.emplace(hash, storage.size());
        group_t &group = storage.emplace_back();
        group.members.push_back(fn.start_ea);
        group.names.push_back(name);
      }
      else
      {
        group_t &group = storage[found->second];
        group.members.push_back(fn.start_ea);
        group.names.push_back(name);
      }
    }
    // Index only interesting groups: >= 2 named members from >= 2
    // different top-level families.
    std::map<ea_t, const group_t *> interesting;
    for ( const group_t &group : storage )
    {
      std::set<qstring> named_families;
      for ( const qstring &name : group.names )
      {
        const qstring family = family_of(name);
        if ( family != "wasmhex_synthetic" )
          named_families.insert(family);
      }
      if ( named_families.size() < 2 )
        continue;
      for ( ea_t member : group.members )
        interesting.emplace(member, &group);
    }
    group_by_member = std::move(interesting);
    {
      if ( options::debug("cpp") )
      {
        size_t folded = 0;
        for ( const auto &entry : group_by_member )
          ++folded;
        msg("WASMHEX_CPP_PROBE icf index: %u functions, %u multi-body groups, "
            "%u interesting functions\n",
            unsigned(quantity), unsigned(storage.size()), unsigned(folded));
      }
    }
    return true;
  }
};

icf_index_t &icf_index()
{
  static icf_index_t index;
  return index;
}

bool read_uleb_from_blob(const uchar *data, size_t size, size_t *offset, uint64 *out)
{
  uint64 result = 0;
  unsigned shift = 0;
  while ( *offset < size && shift < 64 )
  {
    const uchar byte = data[(*offset)++];
    result |= uint64(byte & 0x7Fu) << shift;
    if ( (byte & 0x80u) == 0 )
    {
      *out = result;
      return true;
    }
    shift += 7;
  }
  return false;
}

// Read the multi-export alias list from netnode blob 'E' at the function
// start EA (format v1: [version=1][uleb count][uleb len+utf8]...).
bool load_export_aliases(ea_t fn_ea, qvector<qstring> *out)
{
  netnode wasm_node(wasmhex_node_name, 0, false);
  if ( wasm_node == BADNODE || out == nullptr )
    return false;
  bytevec_t blob;
  if ( wasm_node.getblob_ea(&blob, fn_ea, 'E') <= 0 || blob.empty() )
    return false;
  const uchar *data = blob.begin();
  const size_t size = blob.size();
  if ( data[0] != 1 )
    return false;
  size_t offset = 1;
  uint64 count = 0;
  if ( !read_uleb_from_blob(data, size, &offset, &count) || count < 2 )
    return false;
  out->reserve(size_t(count));
  for ( uint64 i = 0; i < count; ++i )
  {
    uint64 len = 0;
    if ( !read_uleb_from_blob(data, size, &offset, &len) || offset + len > size )
      return false;
    qstring alias(reinterpret_cast<const char *>(data + offset), size_t(len));
    offset += size_t(len);
    out->push_back(alias);
  }
  return offset == size && out->size() > 1;
}

// Resolve a GOT.func.<symbol> name (possibly carrying `orig$` or a `_[0-9]+`
// collision suffix) back to the target function EA in `.text`.
ea_t resolve_got_callee_ea(const char *got_sym)
{
  if ( got_sym == nullptr || *got_sym == '\0' )
    return BADADDR;
  auto try_sym = [](const char *sym) -> ea_t
  {
    if ( sym == nullptr || *sym == '\0' )
      return BADADDR;
    const ea_t ea = get_name_ea(BADADDR, sym);
    if ( ea != BADADDR && get_func_start(ea) != BADADDR )
      return ea;
    qstring stripped;
    if ( strip_collision_suffix(sym, &stripped) )
    {
      const ea_t sea = get_name_ea(BADADDR, stripped.c_str());
      if ( sea != BADADDR && get_func_start(sea) != BADADDR )
        return sea;
    }
    return BADADDR;
  };
  const ea_t direct = try_sym(got_sym);
  if ( direct != BADADDR )
    return direct;
  if ( strncmp(got_sym, "orig$", 5) == 0 )
    return try_sym(got_sym + 5);
  return BADADDR;
}

bool is_table_lookup_helper(const char *name)
{
  // The lookup helper is `wasm_table_lookup` (canonical table0/type0) or
  // `wasm_table_lookup_t<T>_ty<Y>` on multi-table / multi-signature modules.
  return name != nullptr && strncmp(name, "wasm_table_lookup", 17) == 0;
}

bool is_indirect_table_idx(const cexpr_t *e)
{
  return e != nullptr && e->op == cot_idx && e->x != nullptr && e->x->op == cot_helper
    && e->x->helper != nullptr && strcmp(e->x->helper, "__indirect_function_table") == 0
    && e->y != nullptr;
}

cexpr_t *make_indirect_table_idx(const cexpr_t &index_expr, const tinfo_t &elem_type, ea_t ea)
{
  tinfo_t resolved_elem = elem_type;
  if ( resolved_elem.empty() )
    resolved_elem = tinfo_t(BT_INT32);
  tinfo_t table_ptr_type;
  if ( !table_ptr_type.create_ptr(resolved_elem) )
    return nullptr;
  cexpr_t *table = create_helper(true, table_ptr_type, "__indirect_function_table");
  if ( table == nullptr )
    return nullptr;
  table->ea = ea;
  cexpr_t *idx = clone_expr(index_expr);
  if ( idx->type.empty() || (!idx->type.is_integral() && !idx->type.is_enum()) )
    idx->type = tinfo_t(BT_INT32);
  cexpr_t *result = new cexpr_t(cot_idx, table, idx);
  result->ea = ea;
  result->type = resolved_elem;
  return result;
}

// Fold `v_fn = (fn_type)__indirect_function_table[idx]; v_fn(args)`
// (where v_fn is single-write single-read) into
// `__indirect_function_table[idx](args)`. When `idx` is
// `*(_DWORD *)(*(_DWORD *)obj + vtoff)` and `obj == args[0]`, attach a
// `// vtable+<vtoff>` comment on the statement.
void fold_indirect_table_calls(cfunc_t *cfunc)
{
  struct var_counter_t final : public ctree_visitor_t
  {
    std::map<int, int> &reads;
    std::map<int, int> &writes;
    std::map<int, int> &copy_src;

    var_counter_t(std::map<int, int> &r, std::map<int, int> &w, std::map<int, int> &c)
        : ctree_visitor_t(CV_POST | CV_PARENTS), reads(r), writes(w), copy_src(c)
    {
    }

    void count_arg_reads(const cexpr_t *e)
    {
      if ( e == nullptr )
        return;
      if ( e->op == cot_var )
        reads[int(e->v.idx)]++;
      count_arg_reads(e->x);
      count_arg_reads(e->y);
      if ( e->a != nullptr )
        for ( size_t i = 0; i < e->a->size(); ++i )
          count_arg_reads(&(*e->a)[i]);
    }

    int idaapi visit_expr(cexpr_t *e) override
    {
      if ( e->op == cot_var )
      {
        const int idx = int(e->v.idx);
        bool is_write = false;
        if ( !parents.empty() )
        {
          const cexpr_t *parent = static_cast<const cexpr_t *>(parents.back());
          if ( is_assignment(parent->op) && parent->x == e )
          {
            is_write = true;
            if ( parent->op == cot_asg && parent->y != nullptr )
            {
              const cexpr_t *rhs = strip_all_casts(parent->y);
              if ( rhs != nullptr && rhs->op == cot_var )
                copy_src[idx] = int(rhs->v.idx);
            }
          }
        }
        if ( is_write )
          writes[idx]++;
        else
          reads[idx]++;
      }
      else if ( e->op == cot_helper && e->a != nullptr )
      {
        for ( size_t i = 0; i < e->a->size(); ++i )
          count_arg_reads(&(*e->a)[i]);
      }
      return 0;
    }
  };

  std::map<int, int> reads;
  std::map<int, int> writes;
  std::map<int, int> copy_src;
  var_counter_t counter(reads, writes, copy_src);
  counter.apply_to(&cfunc->body, nullptr);

  auto alias_root = [&](int v) -> int
  {
    for ( int depth = 0; depth < 8; ++depth )
    {
      if ( writes[v] != 1 )
        break;
      const auto it = copy_src.find(v);
      if ( it == copy_src.end() || it->second == v || writes[it->second] != 1 )
        break;
      v = it->second;
    }
    return v;
  };

  auto extract_vtable_slot
    = [&](const cexpr_t &idx_expr, const cexpr_t *call_expr, uint64 *vtoff_out) -> bool
  {
    const cexpr_t *outer = strip_all_casts(&idx_expr);
    if ( outer == nullptr || outer->op != cot_ptr || outer->x == nullptr )
      return false;
    const cexpr_t *addr = strip_all_casts(outer->x);
    const cexpr_t *inner_ptr = nullptr;
    uint64 vtoff = 0;
    if ( addr != nullptr && addr->op == cot_add && addr->x != nullptr && addr->y != nullptr )
    {
      const cexpr_t *ax = strip_all_casts(addr->x);
      const cexpr_t *ay = strip_all_casts(addr->y);
      if ( ax != nullptr && ay != nullptr && ax->op == cot_ptr && ay->op == cot_num )
      {
        inner_ptr = ax;
        vtoff = uint64(ay->numval());
      }
      else if ( ax != nullptr && ay != nullptr && ay->op == cot_ptr && ax->op == cot_num )
      {
        inner_ptr = ay;
        vtoff = uint64(ax->numval());
      }
      if ( inner_ptr != nullptr && addr->type.is_ptr() )
      {
        const size_t elem_sz = addr->type.get_pointed_object().get_size();
        if ( elem_sz > 1 && elem_sz != BADSIZE )
          vtoff *= uint64(elem_sz);
      }
    }
    else if ( addr != nullptr && addr->op == cot_ptr )
    {
      inner_ptr = addr;
      vtoff = 0;
    }
    if ( inner_ptr == nullptr || inner_ptr->x == nullptr )
      return false;
    const cexpr_t *obj = strip_all_casts(inner_ptr->x);
    if ( obj == nullptr || obj->op != cot_var )
      return false;
    if ( call_expr == nullptr || call_expr->a == nullptr || call_expr->a->empty() )
      return false;
    const cexpr_t *arg0 = strip_all_casts(&(*call_expr->a)[0]);
    if ( arg0 == nullptr || arg0->op != cot_var )
      return false;
    if ( alias_root(int(obj->v.idx)) != alias_root(int(arg0->v.idx)) )
      return false;
    *vtoff_out = vtoff;
    return true;
  };

  std::set<cinsn_t *> to_remove;

  struct block_folder_t final : public ctree_visitor_t
  {
    cfunc_t *cfunc;
    std::map<int, int> &reads;
    std::map<int, int> &writes;
    std::set<cinsn_t *> &to_remove;
    decltype(extract_vtable_slot) &vtable_check;

    block_folder_t(cfunc_t *_cf, std::map<int, int> &r, std::map<int, int> &w,
                   std::set<cinsn_t *> &rem, decltype(extract_vtable_slot) &vc)
        : ctree_visitor_t(CV_FAST), cfunc(_cf), reads(r), writes(w), to_remove(rem),
          vtable_check(vc)
    {
    }

    static cexpr_t *find_direct_call_in_stmt(cinsn_t &stmt)
    {
      if ( stmt.op == cit_expr && stmt.cexpr != nullptr )
      {
        if ( stmt.cexpr->op == cot_call )
          return stmt.cexpr;
        if ( stmt.cexpr->op == cot_asg && stmt.cexpr->y != nullptr )
        {
          cexpr_t *rhs = const_cast<cexpr_t *>(strip_all_casts(stmt.cexpr->y));
          if ( rhs != nullptr && rhs->op == cot_call )
            return rhs;
        }
      }
      else if ( stmt.op == cit_return && stmt.creturn != nullptr )
      {
        cexpr_t *ret = const_cast<cexpr_t *>(strip_all_casts(&stmt.creturn->expr));
        if ( ret != nullptr && ret->op == cot_call )
          return ret;
      }
      return nullptr;
    }

    int idaapi visit_insn(cinsn_t *insn) override
    {
      if ( insn->op != cit_block || insn->cblock == nullptr )
        return 0;
      cblock_t *b = insn->cblock;
      for ( auto it = b->begin(); it != b->end(); ++it )
      {
        if ( to_remove.count(&*it) != 0 )
          continue;
        if ( it->op != cit_expr || it->cexpr == nullptr || it->cexpr->op != cot_asg
             || it->cexpr->x == nullptr || it->cexpr->x->op != cot_var || it->cexpr->y == nullptr )
          continue;
        const int v_fn = int(it->cexpr->x->v.idx);
        if ( writes[v_fn] != 1 || reads[v_fn] != 1 )
          continue;
        const cexpr_t *rhs = strip_all_casts(it->cexpr->y);
        if ( !is_indirect_table_idx(rhs) )
          continue;
        auto next = it;
        ++next;
        while ( next != b->end() && to_remove.count(&*next) != 0 )
          ++next;
        if ( next == b->end() || next->label_num >= 0 )
          continue;
        cexpr_t *call_expr = find_direct_call_in_stmt(*next);
        if ( call_expr == nullptr || call_expr->x == nullptr )
          continue;
        const cexpr_t *callee_var = strip_all_casts(call_expr->x);
        if ( callee_var == nullptr || callee_var->op != cot_var || int(callee_var->v.idx) != v_fn )
          continue;
        cexpr_t *folded = make_indirect_table_idx(*rhs->y, call_expr->x->type, call_expr->ea);
        if ( folded == nullptr )
          continue;
        uint64 vtoff = 0;
        if ( vtable_check(*rhs->y, call_expr, &vtoff) )
        {
          const ea_t cmt_ea = next->ea != BADADDR ? next->ea : call_expr->ea;
          if ( cmt_ea != BADADDR )
          {
            treeloc_t loc;
            loc.ea = cmt_ea;
            loc.itp = ITP_SEMI;
            qstring cmt;
            cmt.sprnt("vtable+%llu", static_cast<unsigned long long>(vtoff));
            cfunc->set_user_cmt(loc, cmt.c_str());
          }
        }
        call_expr->x->replace_by(folded);
        to_remove.insert(&*it);
      }
      return 0;
    }
  };

  block_folder_t folder(cfunc, reads, writes, to_remove, extract_vtable_slot);
  folder.apply_to(&cfunc->body, nullptr);
  remove_block_statements(cfunc, to_remove);
}
} // namespace

// Display-level rewrites that bring Emscripten-lowered C++ back to its
// source shape. Every transform is pattern-tight
// (specific import names, GOT.func manglings, verified data strings) and
// display-only — the underlying microcode and function names are never
// changed.
class wasmhex_cpp_rewriter_t final : public ctree_visitor_t
{
  cfunc_t *cfunc;
  ea_t data_base = BADADDR;
  bool data_base_loaded = false;

  bool ensure_data_base()
  {
    if ( data_base_loaded )
      return data_base != BADADDR;
    data_base_loaded = true;
    // The data base is the single linear-memory segment the loader
    // maps at wasmhex_memory_base (the netnode 'L' record died with the
    // 0x40000000 mirror). No segment (passive-data modules) -> disabled.
    const ea_t seg_start = get_segment_ea(ea_t(wasmhex_memory_base));
    if ( seg_start != ea_t(wasmhex_memory_base) )
      return false;
    data_base = seg_start;
    return true;
  }

  static bool is_invoke_callee(const cexpr_t *callee)
  {
    return callee->op == cot_helper && strncmp(callee->helper, "invoke_", 7) == 0;
  }

  // invoke_XXX(GOT.func.[orig$]<mangled>[_<N>], rest...)
  // -> the demangled function applied to rest, plus ICF annotation on the
  // resolved target function.
  bool rewrite_invoke_got(cexpr_t *expression)
  {
    const cexpr_t *callee = strip_all_casts(expression->x);
    if ( !is_invoke_callee(callee) || expression->a == nullptr || expression->a->size() < 2 )
      return false;
    const cexpr_t *first = strip_value_wrappers(&(*expression->a)[0]);
    if ( first == nullptr || first->op != cot_obj )
      return false;
    qstring name;
    get_name(&name, first->obj_ea);
    const char *mangled = nullptr;
    if ( strncmp(name.c_str(), "GOT.func._Z", 11) == 0 )
      mangled = name.c_str() + strlen("GOT.func.");
    else if ( strncmp(name.c_str(), "GOT.func.orig$_Z", 16) == 0 )
      mangled = name.c_str() + strlen("GOT.func.orig$");
    else
      return false;
    const ea_t resolved_ea = resolve_got_callee_ea(name.c_str() + strlen("GOT.func."));
    if ( resolved_ea != BADADDR )
      annotate_icf_group(resolved_ea);
    qstring display;
    if ( !demangled_callee_display(mangled, &display) )
      return false;
    carglist_t *list = new carglist_t;
    for ( size_t i = 1; i < expression->a->size(); ++i )
    {
      carg_t argument;
      argument.consume_cexpr(clone_expr((*expression->a)[i]));
      list->push_back(argument);
    }
    cexpr_t *replacement = call_helper(expression->type, list, "%s", display.c_str());
    if ( replacement == nullptr )
      return false;
    replacement->ea = expression->ea;
    expression->replace_by(replacement);
    return true;
  }

  // C++5 (first, tightest cluster): invoke_viii(_cxa_throw, obj, type,
  // dtor) -> throw obj. The type-info and destructor operands only drive
  // unwinding; the visible C++ source is the throw of the exception
  // object.
  bool is_cxa_throw_arg0(const cexpr_t *expression)
  {
    if ( expression->a == nullptr || expression->a->size() != 4 )
      return false;
    qstring name;
    if ( !expression_name(&(*expression->a)[0], &name) )
      return false;
    // The import may carry one or two leading underscores.
    return name == "_cxa_throw" || name == "__cxa_throw";
  }

  // C++3: a call to orig$<mangled> (the Emscripten legalizer's
  // out-of-line copy) renders with the demangled source name.
  bool rewrite_orig_call(cexpr_t *expression)
  {
    const cexpr_t *callee = strip_all_casts(expression->x);
    if ( callee == nullptr || callee->op != cot_obj )
      return false;
    qstring name;
    get_name(&name, callee->obj_ea);
    if ( strncmp(name.c_str(), "orig$_Z", 6) != 0 )
      return false;
    const ea_t orig_target = resolve_got_callee_ea(name.c_str());
    if ( orig_target != BADADDR )
      annotate_icf_group(orig_target);
    qstring display;
    if ( !demangled_callee_display(name.c_str() + strlen("orig$"), &display) )
      return false;
    carglist_t *list = new carglist_t;
    if ( expression->a != nullptr )
      for ( size_t i = 0; i < expression->a->size(); ++i )
      {
        carg_t argument;
        argument.consume_cexpr(clone_expr((*expression->a)[i]));
        list->push_back(argument);
      }
    cexpr_t *replacement = call_helper(expression->type, list, "%s", display.c_str());
    if ( replacement == nullptr )
      return false;
    replacement->ea = expression->ea;
    expression->replace_by(replacement);
    return true;
  }

  bool rewrite_address_of_callee(cexpr_t *expression)
  {
    const cexpr_t *inner = strip_all_casts(expression->x);
    if ( inner == nullptr || inner->op != cot_ref || inner->x == nullptr )
      return false;
    const cexpr_t *pointee = strip_all_casts(inner->x);
    if ( pointee == nullptr || pointee->op != cot_obj )
      return false;
    if ( get_func_start(pointee->obj_ea) == BADADDR )
      return false;
    annotate_icf_group(pointee->obj_ea);
    qstring pointee_name;
    get_name(&pointee_name, pointee->obj_ea);
    if ( pointee_name.empty() )
      return false;
    qstring display;
    if ( strncmp(pointee_name.c_str(), "_Z", 2) == 0 )
    {
      if ( !demangled_callee_display(pointee_name.c_str(), &display) )
        display = pointee_name;
    }
    else if ( strncmp(pointee_name.c_str(), "orig$_Z", 7) == 0 )
    {
      if ( !demangled_callee_display(pointee_name.c_str() + strlen("orig$"), &display) )
        display = pointee_name;
    }
    else
    {
      display = pointee_name;
    }
    carglist_t *list = new carglist_t;
    if ( expression->a != nullptr )
    {
      for ( size_t i = 0; i < expression->a->size(); ++i )
      {
        carg_t argument;
        argument.consume_cexpr(clone_expr((*expression->a)[i]));
        list->push_back(argument);
      }
    }
    cexpr_t *replacement = call_helper(expression->type, list, "%s", display.c_str());
    if ( replacement == nullptr )
      return false;
    replacement->ea = expression->ea;
    expression->replace_by(replacement);
    return true;
  }

  // C++4: __memory_base + K (used as a char* argument) -> string literal
  // when K addresses one in the DATA mirror. The base arrives either as
  // the imported global object or (Emscripten prologue) a local holding
  // its value — both spellings match by name.
  bool is_memory_base_operand(const cexpr_t *operand)
  {
    if ( operand->op == cot_obj )
    {
      qstring name;
      get_name(&name, operand->obj_ea);
      return name.find("memory_base") != qstring::npos;
    }
    if ( operand->op == cot_var )
    {
      const lvars_t *lvars = cfunc->get_lvars();
      if ( lvars == nullptr || operand->v.idx >= lvars->size() )
        return false;
      const qstring &name = (*lvars)[operand->v.idx].name;
      return name.find("memory_base") != qstring::npos;
    }
    return false;
  }

  bool rewrite_memory_base_string(cexpr_t *expression)
  {
    const cexpr_t *left = strip_all_casts(expression->x);
    const cexpr_t *right = strip_all_casts(expression->y);
    if ( left == nullptr || right == nullptr )
      return false;
    const cexpr_t *object = nullptr;
    const cexpr_t *number = nullptr;
    if ( is_memory_base_operand(left) && right->op == cot_num )
    {
      object = left;
      number = right;
    }
    else if ( is_memory_base_operand(right) && left->op == cot_num )
    {
      object = right;
      number = left;
    }
    else
      return false;
    (void)object;
    if ( !ensure_data_base() )
      return false;
    qstring text;
    ea_t data_ea = BADADDR;
    if ( !memory_base_string(data_base, uint64(number->numval()), &text, &data_ea) )
      return false;
    // Anchor at the string's data EA (see make_go_string_literal): an
    // instruction-EA anchor lands a .text "Decompiler" row in the
    // Strings window via the IDB synthetic-string registry.
    cexpr_t *literal = make_string_literal(text);
    literal->ea = data_ea;
    expression->replace_by(literal);
    return true;
  }

public:
  explicit wasmhex_cpp_rewriter_t(cfunc_t *_cfunc) : ctree_visitor_t(CV_POST), cfunc(_cfunc)
  {
    trace_probe = options::debug("cpp");
    // Annotate the decompiled function itself when its
    // body carries multi-export ICF aliases or cross-family identical bytes.
    annotate_icf_group(_cfunc->entry_ea);
  }

  bool trace_probe = false;

  void annotate_icf_group(ea_t fn_ea)
  {
    fn_ea = get_func_start(fn_ea);
    if ( fn_ea == BADADDR )
      return;
    qstring existing;
    get_func_cmt_ea(&existing, fn_ea, true);
    if ( existing.find("wasmdec ICF-folded") != qstring::npos )
      return; // already annotated

    qstring self_name;
    get_name(&self_name, fn_ea);
    qvector<qstring> other_names;
    std::set<qstring> seen;
    if ( !self_name.empty() )
      seen.insert(self_name);

    qvector<qstring> export_aliases;
    if ( load_export_aliases(fn_ea, &export_aliases) )
    {
      for ( const qstring &alias : export_aliases )
      {
        if ( !alias.empty() && seen.insert(alias).second )
          other_names.push_back(alias);
      }
    }

    const icf_index_t::group_t *group = icf_index().group_of(fn_ea);
    if ( group != nullptr )
    {
      for ( size_t i = 0; i < group->members.size(); ++i )
      {
        if ( group->members[i] == fn_ea )
          continue;
        if ( !group->names[i].empty() && seen.insert(group->names[i]).second )
          other_names.push_back(group->names[i]);
      }
    }

    if ( other_names.empty() )
      return;

    qstring text = "wasmdec ICF-folded with:";
    const size_t others = other_names.size();
    const size_t listed = others < 4 ? others : 4;
    for ( size_t i = 0; i < listed; ++i )
    {
      text += " ";
      text += other_names[i];
    }
    qstring tail;
    tail.sprnt(" (+%u more)", unsigned(others - listed));
    text += tail;
    if ( !existing.empty() )
    {
      existing += "\n";
      existing += text;
      text = existing;
    }
    set_func_cmt_ea(fn_ea, text.c_str(), true);
    if ( trace_probe )
      msg("WASMHEX_CPP_PROBE icf %a -> %s\n", fn_ea, text.c_str());
  }

  int idaapi leave_expr(cexpr_t *expression) override
  {
    if ( expression->op == cot_call && expression->x != nullptr )
    {
      // Calls into an ICF-folded body annotate the callee.
      const cexpr_t *direct_callee = strip_value_wrappers(expression->x);
      if ( direct_callee != nullptr && direct_callee->op == cot_obj )
        annotate_icf_group(direct_callee->obj_ea);
      if ( rewrite_invoke_got(expression) )
      {
        if ( trace_probe )
          msg("WASMHEX_CPP_PROBE invoke_got %a\n", expression->ea);
        return 0;
      }
      if ( rewrite_orig_call(expression) )
      {
        if ( trace_probe )
          msg("WASMHEX_CPP_PROBE orig %a\n", expression->ea);
        return 0;
      }
      if ( rewrite_address_of_callee(expression) )
      {
        if ( trace_probe )
          msg("WASMHEX_CPP_PROBE addr_of %a\n", expression->ea);
        return 0;
      }
      return 0;
    }
    if ( expression->op == cot_add && rewrite_memory_base_string(expression) && trace_probe )
      msg("WASMHEX_CPP_PROBE membase %a\n", expression->ea);
    return 0;
  }

  // C++5: the throw rewrite replaces the whole statement (throw is a
  // statement in the ctree, not an expression).
  int idaapi visit_insn(cinsn_t *insn) override
  {
    if ( insn->op != cit_expr || insn->cexpr == nullptr || insn->cexpr->op != cot_call )
      return 0;
    cexpr_t *call = insn->cexpr;
    const cexpr_t *callee = strip_all_casts(call->x);
    const bool direct_throw = callee != nullptr && callee->op == cot_helper
      && (strcmp(callee->helper, "_cxa_throw") == 0 || strcmp(callee->helper, "__cxa_throw") == 0)
      && call->a != nullptr && call->a->size() == 3;
    const bool invoke_throw = is_invoke_callee(callee) && is_cxa_throw_arg0(call);
    if ( !direct_throw && !invoke_throw )
      return 0;
    cexpr_t *thrown = nullptr;
    if ( invoke_throw )
      thrown = clone_expr((*call->a)[1]);
    else
      thrown = clone_expr((*call->a)[0]);
    if ( trace_probe )
      msg("WASMHEX_CPP_PROBE throw %a\n", call->ea);
    insn->cleanup();
    insn->op = cit_throw;
    insn->cthrow = new cthrow_t;
    insn->cthrow->expr.swap(*thrown);
    delete thrown;
    insn->ea = call->ea;
    return 0;
  }
};

class wasmhex_readable_idiom_rewriter_t final : public ctree_visitor_t
{
  cfunc_t *cfunc;
  bool hidden_mode = false;
  tinfo_t i32_ptr_type;
  tinfo_t i64_ptr_type;

  static cexpr_t *clone(const cexpr_t &expression) { return new cexpr_t(expression); }

  static bool parse_type_once(const char *declaration, tinfo_t *out)
  {
    return out->parse(declaration, get_idati(), PT_SIL);
  }

  bool ensure_pointer_types()
  {
    if ( !i32_ptr_type.empty() )
      return true;
    return parse_type_once("int *x;", &i32_ptr_type)
      && parse_type_once("__int64 *x;", &i64_ptr_type);
  }

  static bool is_select_helper(const char *name)
  {
    if ( name == nullptr )
      return false;
    return strcmp(name, "wasm_select_i32") == 0 || strcmp(name, "wasm_select_i64") == 0
      || strcmp(name, "wasm_select_f32") == 0 || strcmp(name, "wasm_select_f64") == 0
      || strcmp(name, "wasm_select_ref") == 0;
  }

  enum atomic_rewrite_t
  {
    atomic_none,
    atomic_load,
    atomic_store,
    atomic_rmw,
  };

  // Matches full-width seq_cst helpers only:
  //   wasm_memory0_i32_atomic_rmw_add_seq_cst(addr, value, offset)
  //   wasm_memory0_i32_atomic_load_seq_cst(addr, offset)
  //   wasm_memory0_i32_atomic_store_seq_cst(addr, value, offset)
  // Narrow (rmw8/16, load_u) and cmpxchg/wait/notify forms stay helpers.
  static atomic_rewrite_t decode_atomic(const char *name, const char **operation, int *value_size)
  {
    if ( name == nullptr || strncmp(name, "wasm_memory0_", 13) != 0 )
      return atomic_none;
    const char *rest = name + 13;
    int size = 0;
    if ( strncmp(rest, "i32_atomic_", 11) == 0 )
    {
      size = 4;
      rest += 11;
    }
    else if ( strncmp(rest, "i64_atomic_", 11) == 0 )
    {
      size = 8;
      rest += 11;
    }
    else
      return atomic_none;
    if ( strcmp(rest, "load_seq_cst") == 0 )
    {
      *value_size = size;
      return atomic_load;
    }
    if ( strcmp(rest, "store_seq_cst") == 0 )
    {
      *value_size = size;
      return atomic_store;
    }
    if ( strncmp(rest, "rmw_", 4) == 0 )
    {
      rest += 4;
      static const char *const operations[] = {
        "add", "sub", "and", "or", "xor", "xchg",
      };
      for ( const char *candidate : operations )
      {
        const size_t length = strlen(candidate);
        if ( strncmp(rest, candidate, length) != 0 )
          continue;
        if ( strcmp(rest + length, "_seq_cst") != 0 )
          continue;
        *operation = candidate;
        *value_size = size;
        return atomic_rmw;
      }
    }
    return atomic_none;
  }

  static cexpr_t *cast_to(cexpr_t *value, const tinfo_t &type, ea_t ea)
  {
    if ( value->type.get_size() == type.get_size() && value->type.get_sign() == type.get_sign() )
    {
      value->type = type;
      return value;
    }
    cexpr_t *result = new cexpr_t(cot_cast, value);
    result->ea = ea;
    result->type = type;
    return result;
  }

  cexpr_t *builtin_call(const cexpr_t &call, const char *name, const cexpr_t *const *arguments,
                        size_t count)
  {
    carglist_t *list = new carglist_t;
    for ( size_t i = 0; i < count; ++i )
    {
      carg_t argument;
      argument.consume_cexpr(clone(*arguments[i]));
      list->push_back(argument);
    }
    cexpr_t *result = call_helper(call.type, list, "%s", name);
    if ( result != nullptr )
      result->ea = call.ea;
    return result;
  }

public:
  explicit wasmhex_readable_idiom_rewriter_t(cfunc_t *_cfunc)
      : ctree_visitor_t(CV_POST), cfunc(_cfunc)
  {
    netnode wasm_node(wasmhex_node_name, 0, false);
    wasmhex_bounds_mode_t mode = wasmhex_bounds_mode_t::explicit_bounds;
    if ( wasm_node != BADNODE && get_wasm_bounds_mode(wasm_node, &mode) )
      hidden_mode = mode == wasmhex_bounds_mode_t::hidden;
  }

  bool is_hidden_mode() const { return hidden_mode; }

  int idaapi leave_expr(cexpr_t *expression) override
  {
    if ( !hidden_mode || expression == nullptr )
      return 0;

    // Absorb a function-pointer cast directly into a standalone
    // `__indirect_function_table[idx]` node so it renders without a cast.
    if ( expression->op == cot_cast && expression->x != nullptr
         && is_indirect_table_idx(expression->x) )
    {
      cexpr_t *retyped
        = make_indirect_table_idx(*expression->x->y, expression->type, expression->ea);
      if ( retyped != nullptr )
        expression->replace_by(retyped);
      return 0;
    }

    if ( expression->op != cot_call || expression->x == nullptr || expression->x->op != cot_helper
         || expression->a == nullptr )
      return 0;
    const ea_t ea = expression->ea;

    // wasm_table_lookup[_tT_tyY](idx) ->
    // __indirect_function_table[idx] in bounds=hidden mode.
    if ( is_table_lookup_helper(expression->x->helper) )
    {
      if ( expression->a->size() != 1 )
        return 0;
      cexpr_t *replacement = make_indirect_table_idx((*expression->a)[0], expression->type, ea);
      if ( replacement != nullptr )
        expression->replace_by(replacement);
      return 0;
    }

    if ( is_select_helper(expression->x->helper) )
    {
      if ( expression->a->size() != 3 )
        return 0;
      if ( (*expression->a)[0].has_side_effects() || (*expression->a)[1].has_side_effects() )
      {
        return 0; // Keep the helper — the unselected arm must still run
      }
      cexpr_t *condition = clone((*expression->a)[2]);
      cexpr_t *yes = clone((*expression->a)[0]);
      cexpr_t *no = clone((*expression->a)[1]);
      cexpr_t *replacement = new cexpr_t(cot_tern, condition, yes, no);
      replacement->ea = ea;
      replacement->type = expression->type;
      expression->replace_by(replacement);
      return 0;
    }

    // Full-width seq_cst atomics -> GCC __atomic builtins.
    const char *operation = nullptr;
    int value_size = 0;
    const atomic_rewrite_t kind = decode_atomic(expression->x->helper, &operation, &value_size);
    if ( kind == atomic_none || !ensure_pointer_types() )
      return 0;
    const size_t expected = kind == atomic_load ? 2 : 3;
    if ( expression->a->size() != expected )
      return 0;
    // The trailing memarg offset must be a plain zero constant; anything
    // else keeps the explicit helper (fail-closed).
    const cexpr_t &offset = (*expression->a)[expected - 1];
    if ( offset.op != cot_num || offset.numval() != 0 )
      return 0;
    const tinfo_t &pointer_type = value_size == 4 ? i32_ptr_type : i64_ptr_type;
    cexpr_t *address = cast_to(clone((*expression->a)[0]), pointer_type, ea);
    if ( address == nullptr )
      return 0;
    // 5 == __ATOMIC_SEQ_CST
    cexpr_t *memorder = make_num(uint64(5), cfunc, ea, 0, no_sign, 4);
    if ( memorder == nullptr )
      return 0;

    switch ( kind )
    {
      case atomic_load:
      {
        const cexpr_t *arguments[] = { address, memorder };
        cexpr_t *replacement = builtin_call(*expression, "__atomic_load_n", arguments, 2);
        delete memorder;
        if ( replacement != nullptr )
          expression->replace_by(replacement);
        return 0;
      }
      case atomic_store:
      case atomic_rmw:
      {
        cexpr_t *value = clone((*expression->a)[1]);
        const char *name = "__atomic_store_n";
        if ( kind == atomic_rmw )
        {
          name = strcmp(operation, "xchg") == 0 ? "__atomic_exchange_n"
            : strcmp(operation, "add") == 0     ? "__atomic_fetch_add"
            : strcmp(operation, "sub") == 0     ? "__atomic_fetch_sub"
            : strcmp(operation, "and") == 0     ? "__atomic_fetch_and"
            : strcmp(operation, "or") == 0      ? "__atomic_fetch_or"
                                                : "__atomic_fetch_xor";
        }
        const cexpr_t *arguments[] = { address, value, memorder };
        cexpr_t *replacement = builtin_call(*expression, name, arguments, 3);
        delete memorder;
        if ( replacement != nullptr )
          expression->replace_by(replacement);
        return 0;
      }
      default: return 0;
    }
  }
};

// Entry point used by the CMAT_FINAL pipeline in ctree.cpp.
void wasmhex_apply_idiom_rewriter(cfunc_t *cfunc)
{
  wasmhex_readable_idiom_rewriter_t rewriter(cfunc);
  rewriter.apply_to(&cfunc->body, nullptr);
  if ( rewriter.is_hidden_mode() )
    fold_indirect_table_calls(cfunc);
}

// C++1-C++8: entry point used by the CMAT_FINAL pipeline after the EH
// rewriter (throw detection needs the invokes it leaves behind).
void wasmhex_apply_cpp_rewriter(cfunc_t *cfunc)
{
  wasmhex_cpp_rewriter_t rewriter(cfunc);
  rewriter.apply_to(&cfunc->body, nullptr);
  netnode wasm_node(wasmhex_node_name, 0, false);
  wasmhex_bounds_mode_t mode = wasmhex_bounds_mode_t::explicit_bounds;
  if ( wasm_node != BADNODE && get_wasm_bounds_mode(wasm_node, &mode)
       && mode == wasmhex_bounds_mode_t::hidden )
    fold_indirect_table_calls(cfunc);
}

} // namespace wasmhex

#endif // WASMHEX_PURE_STRING_TEST
