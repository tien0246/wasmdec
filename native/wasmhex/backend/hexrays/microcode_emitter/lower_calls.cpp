// Touch anchor: structural terminal noreturn callees.
#include "filter.hpp"
#include "../../../backend/idb/metadata_schema/metadata.hpp"
#include "../../../backend/idb/metadata_schema/netnode_tags.hpp"
#include "../../../ida/loader/options.hpp"
#include "../../../core/analysis/param_pointers.hpp"
#include <algorithm>
#include <cstring>
#include <vector>
namespace wasmhex
{
namespace
{
// C string args: the per-call-site proof. True (and records a CMAT_FINAL
// swap) only when EVERY condition holds:
//   * a plain direct/import call (no typed helper, no dispatch unwind /
//     reference / checked-indirect / tail machinery, not a Go-shaped call —
//     those render through their own presentation layer),
//   * the argument position is pointer-typed: known-libc table (by callee
//     name), the callee's declared prototype (arity-matched, so an sret
//     `void *__ret` can never shift the positions), or the loader's
//     body-proof mask blob '4' (function-index keyed),
//   * the argument is a nonzero i32 constant resolving inside the single
//     linear-memory segment (initialized data or BSS),
//   * the display: a NUL-terminated printable span (capped at 1 MB) becomes
//     a string literal; anything else becomes &item — and only when an item
//     with a name exists there (the loader's kind-4 dref created it; an old
//     IDB simply keeps the numeric display — fail-closed).
bool try_record_c_pointer_argument(ea_t state_entry, codegen_t &cdg, bool imported, ea_t target,
                                   uint32 function_index, const qstring &helper_name,
                                   const qvector<uchar> &callee_param_types, size_t arg_index,
                                   const wasm_value_t &argument, bool go_shaped)
{
  if ( go_shaped )
    return false; // Go presentation owns those sites
  if ( arg_index >= callee_param_types.size() || callee_param_types[arg_index] != 0x7F )
    return false;
  if ( options::debug("csarg") )
  {
    char db[144];
    int dn = qsnprintf(db, sizeof(db), "CSARG ea=%a i=%zu imported=%d\n", cdg.insn.ea, arg_index,
                       int(imported));
    if ( dn > 0 )
      ::write(2, db, size_t(dn));
  }
  // The constant, loader-proven when present (blob '3' at the call EA):
  // an `i32.const` that reaches the call through a local no longer carries
  // const-ness on the operand here — the walk's record is the same proof
  // from the same instruction stream. Direct operand constants cover the
  // remaining decompiler-side sources (import table / callee prototype).
  uint64 address = 0;
  bool have_address = false;
  {
    netnode wasm_node(wasmhex_node_name, 0, false);
    if ( wasm_node != BADNODE )
    {
      uchar blob_header[8];
      if ( wasm_node.supval_ea(cdg.insn.ea, blob_header, sizeof(blob_header),
                               uchar(wasmhex::tag_t::pointer_arg_records))
           == sizeof(blob_header) )
      {
        uint32 magic = 0;
        memcpy(&magic, blob_header, sizeof(magic));
        const uchar count = blob_header[4];
        if ( magic == 0x52475241u && count != 0 )
        {
          std::vector<uchar> blob(sizeof(blob_header) + size_t(count) * 16);
          if ( wasm_node.supval_ea(cdg.insn.ea, blob.data(), blob.size(),
                                   uchar(wasmhex::tag_t::pointer_arg_records))
               == ssize_t(blob.size()) )
          {
            for ( size_t e = 0; e < count && !have_address; ++e )
            {
              const uchar *entry = blob.data() + sizeof(blob_header) + e * 16;
              uint64 recorded = 0;
              memcpy(&recorded, entry + 8, sizeof(recorded));
              if ( size_t(entry[0]) == arg_index && recorded != 0 )
              {
                address = recorded;
                have_address = true;
              }
            }
          }
        }
      }
    }
  }
  if ( !have_address )
  {
    uint64 raw = 0;
    if ( argument.has_constant )
      raw = argument.constant;
    else if ( !argument.op.is_constant(&raw, false) )
      return false;
    address = uint32(raw);
  }
  if ( address == 0 )
    return false; // NULL stays numeric
  // getseg is deprecated (SDK >= 9.4); segment_info_t::start_ea is the
  // same linear-memory base check without a segment_t pointer.
  segment_info_t segment;
  if ( !get_segment_info(&segment, ea_t(address)) || segment.start_ea != ea_t(wasmhex_memory_base) )
    return false; // not the linear-memory segment: no honest item there
  // Span gate: only the initialized span or the BSS ABOVE it counts as a
  // "valid data span" — a constant below the span start
  // stays numeric so no low item re-anchors unrelated displays.
  {
    netnode wasm_node(wasmhex_node_name, 0, false);
    uint64 span[2] = { 0, 0 };
    if ( wasm_node == BADNODE || wasm_node.supval(0, &span, sizeof(span), 'L') != sizeof(span)
         || address < span[0] )
      return false;
  }

  // Pointer-typed position?
  uint64 pointer_mask = 0;
  qstring callee_name;
  if ( imported && !helper_name.empty() )
    callee_name = helper_name;
  else if ( !imported && target != BADADDR && target != 0 )
    get_func_name(&callee_name, target);
  if ( !callee_name.empty() )
    pointer_mask |= loader::libc_pointer_mask(std::string(callee_name.c_str()));
  if ( !imported && target != BADADDR && target != 0 )
  {
    tinfo_t callee_tinfo;
    if ( get_tinfo(&callee_tinfo, target) && callee_tinfo.is_func() )
    {
      func_type_data_t details;
      if ( callee_tinfo.get_func_details(&details) && details.size() == callee_param_types.size() )
      {
        for ( size_t j = 0; j < details.size() && j < 64; ++j )
          if ( details[j].type.is_ptr() )
            pointer_mask |= uint64(1) << j;
      }
    }
  }
  if ( !imported )
  {
    netnode wasm_node(wasmhex_node_name, 0, false);
    if ( wasm_node != BADNODE )
    {
      uchar header[8];
      if ( wasm_node.supval(0, header, sizeof(header), uchar(wasmhex::tag_t::param_pointer_masks))
           == sizeof(header) )
      {
        uint32 words[2];
        memcpy(words, header, sizeof(words));
        if ( words[0] == 0x50505243u && function_index < words[1] )
        {
          std::vector<uchar> blob(sizeof(header) + sizeof(uint64) * words[1]);
          if ( wasm_node.supval(0, blob.data(), blob.size(),
                                uchar(wasmhex::tag_t::param_pointer_masks))
               == ssize_t(blob.size()) )
          {
            uint64 mask = 0;
            memcpy(&mask, blob.data() + sizeof(header) + sizeof(uint64) * function_index,
                   sizeof(mask));
            pointer_mask |= mask;
          }
        }
      }
    }
  }
  if ( arg_index >= 64 || (pointer_mask & (uint64(1) << arg_index)) == 0 )
    return false;

  // Literal (printable, NUL-terminated, capped) or &item?
  const uint64 span_end = uint64(segment.end_ea);
  const uint64 cap = std::min<uint64>(uint64(1) << 20, span_end - address);
  std::string text;
  bool literal = false;
  for ( uint64 at = address; at < address + cap && !literal; )
  {
    const size_t want = size_t(std::min<uint64>(4096, address + cap - at));
    std::vector<uint8> chunk(want);
    const ssize_t got = get_bytes(chunk.data(), want, ea_t(at));
    if ( got <= 0 )
      break; // uninitialized tail (BSS zero-fill): not a printable span
    bool stop = false;
    for ( size_t b = 0; b < size_t(got) && !stop; ++b )
    {
      const uint8 byte = chunk[b];
      if ( byte == 0 )
      {
        literal = !text.empty();
        stop = true;
        break;
      }
      const bool printable
        = byte == 0x09 || byte == 0x0A || byte == 0x0D || (byte >= 0x20 && byte <= 0x7E);
      if ( !printable )
      {
        stop = true; // binary data: the item form below
        break;
      }
      text.push_back(char(byte));
    }
    if ( stop )
      break;
    at += size_t(got);
    if ( got < ssize_t(want) )
      break; // truncated read: no NUL proven
  }
  if ( literal )
  {
    // Anchor the literal at the linear-memory EA the span was read at —
    // never the call site (Strings-window pollution; see filter.hpp).
    record_c_arg_string_literal(state_entry, cdg.insn.ea, arg_index, text, ea_t(address));
    return true;
  }
  const ea_t item_ea = get_item_head(ea_t(address));
  qstring item_name;
  if ( item_ea != BADADDR && get_name(&item_name, item_ea) > 0 && !item_name.empty() )
  {
    record_c_arg_data_address(state_entry, cdg.insn.ea, arg_index, item_ea);
    return true;
  }
  return false; // no item (old IDB without the loader dref): numeric stays
}
} // namespace

// Build marker, verified via strings on the deployed procs dylib (the
// pattern from core/toolchains/go/types.cpp).
extern "C" __attribute__((used)) const char kWasmhexG7ArgAggregatesMarker[]
  = "WASMHEX_BUILD_MARKER_G7_ARG_AGGREGATES";

bool wasmhex_filter_t::emit_call_result_move(codegen_t &cdg, const mop_t &src, const mop_t &dst,
                                             bool fp)
{
  if ( src == dst )
    return true;
  minsn_t *ins = cdg.emit(m_mov, &src, nullptr, &dst);
  if ( ins == nullptr )
    return false;
  ins->clr_propagatable();
  if ( fp )
    ins->set_fpinsn();
  return true;
}

bool wasmhex_filter_t::emit_sret_store(codegen_t &cdg, const mop_t &value, size_t offset, bool fp)
{
  if ( !has_sret_pointer || sret_pointer_reg == mr_none )
    return false;
  const mop_t sret_pointer(sret_pointer_reg, 4);
  if ( inf_is_64bit() )
  {
    mop_t wide_pointer;
    if ( !make_scratch_register(cdg, 8, 0, &wide_pointer) )
      return false;
    if ( cdg.emit(m_xdu, &sret_pointer, nullptr, &wide_pointer) == nullptr )
      return false;
    mop_t address = wide_pointer;
    if ( offset != 0 )
    {
      mop_t destination;
      if ( !make_scratch_register(cdg, 8, 1, &destination) )
        return false;
      mop_t displacement;
      displacement.make_number(uint64(wasmhex_memory_base) + offset, 8, cdg.insn.ea, 0);
      if ( cdg.emit(m_add, &address, &displacement, &destination) == nullptr )
        return false;
      address = destination;
    }
    else if ( wasmhex_memory_base != 0 )
    {
      // Even without a member offset the linear-memory base must be folded
      // in so the deref lands inside the wasm segment. With the base
      // of 0 the zero-extension alone already carries the exact EA.
      mop_t destination;
      if ( !make_scratch_register(cdg, 8, 1, &destination) )
        return false;
      mop_t base;
      base.make_number(uint64(wasmhex_memory_base), 8, cdg.insn.ea, 0);
      if ( cdg.emit(m_add, &address, &base, &destination) == nullptr )
        return false;
      address = destination;
    }
    mop_t selector;
    selector.make_reg(reg2mreg(wasmhex_ds_reg), 2);
    minsn_t *store = cdg.emit(m_stx, &value, &selector, &address);
    if ( store == nullptr )
      return false;
    if ( fp )
      store->set_fpinsn();
    return true;
  }
  mop_t address = sret_pointer;
  if ( offset != 0 )
  {
    mop_t destination;
    if ( !make_scratch_register(cdg, 4, 0, &destination) )
      return false;
    mop_t displacement;
    displacement.make_number(offset, 4, cdg.insn.ea, 0);
    if ( cdg.emit(m_add, &sret_pointer, &displacement, &destination) == nullptr )
      return false;
    address = destination;
  }
  mop_t selector;
  selector.make_reg(reg2mreg(wasmhex_ds_reg), 2);
  minsn_t *store = cdg.emit(m_stx, &value, &selector, &address);
  if ( store == nullptr )
    return false;
  if ( fp )
    store->set_fpinsn();
  return true;
}

merror_t wasmhex_filter_t::emit_sparse_indirect_call(codegen_t &cdg)
{
  netnode wasm_node(wasmhex_node_name, 0, false);
  ea_t continuation = BADADDR;
  wasmhex_sparse_dispatch_t sparse_dispatch = {};
  uint32 case_count = 0;
  qvector<uchar> parameter_types;
  qvector<uchar> result_types;
  const uint32 type_index = uint32(cdg.insn.Op1.value);
  const bool tail_call = cdg.insn.itype == wasmhex_return_call_indirect;
  uint64 table_minimum = 0;
  uchar table_flags = 0;
  if ( wasm_node == BADNODE || controls.empty()
       || (!tail_call
           && wasm_node.supval_ea(cdg.insn.ea, &continuation, sizeof(continuation), 'C')
             != sizeof(continuation))
       || wasm_node.supval_ea(cdg.insn.ea, &sparse_dispatch, sizeof(sparse_dispatch), 'J')
         != sizeof(sparse_dispatch)
       || !get_dispatch_case_count(wasm_node, cdg.insn.ea, &case_count)
       || case_count > WASMHEX_MAX_SPARSE_TABLE_DISPATCH_CASES
       || case_count > sparse_dispatch.table_size
       || sparse_dispatch.target_count > sparse_dispatch.table_size
       || sparse_dispatch.target_count > case_count
       || (tail_call && sparse_dispatch.table_size == 0)
       || !get_type_vector(wasm_node, type_index, 'A', &parameter_types)
       || !get_type_vector(wasm_node, type_index, 'B', &result_types) )
  {
    msg("WASMHEX_F5_UNSUPPORTED: sparse call_indirect dispatch metadata "
        "rejected at %a\n",
        cdg.insn.ea);
    return MERR_BADARCH;
  }
  if ( get_wasm_table_metadata(wasm_node, cdg.insn.Op2.value, nullptr, &table_minimum, &table_flags)
       != wasm_table_metadata_status_t::ok )
    return MERR_BADARCH;
  const auto is_numeric_scalar = [](uchar type)
  {
    return type == 0x7F || type == 0x7E || type == 0x7D || type == 0x7C;
  };
  for ( uchar type : parameter_types )
    if ( !is_numeric_scalar(type) )
      return MERR_BADARCH;
  const size_t result_size = wasm_results_size(result_types);
  size_t result_words = 0;
  if ( result_size > size_t(std::numeric_limits<int>::max())
       || (result_types.size() > 1
           && !multi_result_word_count(result_types, &result_words, result_size > 16))
       || (result_types.size() == 1 && !is_numeric_scalar(result_types[0])) )
    return MERR_BADARCH;
  if ( tail_call && result_types.size() > 1 && result_size <= 16
       && (result_types.size() != 2 || result_types[0] != 0x7F
           || (result_types[1] != 0x7F && result_types[1] != 0x70 && result_types[1] != 0x6F)) )
    return MERR_BADARCH;
  if ( tail_call && result_types != function_results )
    return MERR_BADARCH;
  if ( stack.size() < controls.back().base_stack.size() + parameter_types.size() + 1 )
    return MERR_BADARCH;

  wasm_value_t table_index;
  const uchar table_index_type = (table_flags & 2) != 0 ? uchar(0x7E) : uchar(0x7F);
  if ( !pop(table_index_type, &table_index) )
    return MERR_BADARCH;
  wasm_indirect_dispatch_t dispatch;
  dispatch.source = cdg.insn.ea;
  dispatch.continuation = continuation;
  dispatch.table_size = sparse_dispatch.table_size;
  dispatch.tail_call = tail_call;
  dispatch.result_types = result_types;
  dispatch.arguments.resize(parameter_types.size());
  for ( size_t i = parameter_types.size(); i != 0; --i )
    if ( !pop(parameter_types[i - 1], &dispatch.arguments[i - 1],
              has_c_frame && !tail_call && parameter_types[i - 1] == 0x7F) )
      return MERR_BADARCH;
  dispatch.base_stack = stack;
  dispatch.continuation_stack = stack;
  // Sparse dispatch selector must match the table index width: i64 tables
  // (table64) index with 8-byte selectors, plain i32 tables with 4. The
  // dispatch stubs already compare against dispatch->selector.size.
  if ( !make_scratch_register(cdg, wasm_type_size(table_index_type), 0, &dispatch.selector) )
    return MERR_MEM;
  if ( !emit_move(cdg, table_index.op, dispatch.selector, false) )
    return MERR_INTERR;
  if ( !tail_call )
  {
    for ( size_t i = 0; i < result_types.size(); ++i )
    {
      const uchar type = result_types[i];
      wasm_value_t &result = dispatch.continuation_stack.push_back();
      if ( !make_operand_register(cdg, type, dispatch.base_stack.size() + i, &result.op) )
        return MERR_MEM;
      result.type = type;
    }
  }
  if ( find_indirect_dispatch(dispatch.source) != nullptr )
    return MERR_BADARCH;
  if ( !tail_call && sparse_dispatch.target_count != 0
       && std::none_of(resume_states.begin(), resume_states.end(),
                       [&](const wasm_branch_resume_t &item)
                       { return item.target == continuation; }) )
  {
    wasm_branch_resume_t resume;
    resume.target = continuation;
    resume.stack = dispatch.continuation_stack;
    resume_states.push_back(resume);
  }
  indirect_dispatches.push_back(dispatch);
  stack = tail_call ? dispatch.base_stack : dispatch.continuation_stack;
  if ( tail_call )
    unreachable = true;
  // No stub stream exists — the dispatch chain is built right here
  // from the packed 'j' case blob (slot/function/trap triplets stored at
  // the call source EA by the loader).
  qvector<uint32> dispatch_cases;
  if ( !load_dispatch_cases(cdg.insn.ea, &dispatch_cases)
       || dispatch_cases.size() / 3 != case_count )
  {
    msg("WASMHEX_F5_UNSUPPORTED: sparse dispatch case blob missing or "
        "inconsistent at %a\n",
        cdg.insn.ea);
    return MERR_BADARCH;
  }
  return emit_dispatch_chain(cdg, dispatch, continuation, dispatch_cases);
}

bool wasmhex_filter_t::load_dispatch_cases(ea_t source, qvector<uint32> *cases)
{
  // The packed 'j' blob is loader-written and immutable after load, so
  // the decoded case table is memoized on the per-function cache (first
  // F5 reads the netnode, re-F5s hit the memo).
  if ( active_function_ir != nullptr )
  {
    const auto memo = active_function_ir->dispatch_cases.find(source);
    if ( memo != active_function_ir->dispatch_cases.end() )
    {
      cases->clear();
      cases->reserve(memo->second.size());
      for ( const std::uint32_t value : memo->second )
        cases->push_back(value);
      return true;
    }
    netnode wasm_node(wasmhex_node_name, 0, false);
    if ( wasm_node != BADNODE && read_dispatch_cases(wasm_node, source, cases) )
    {
      std::vector<std::uint32_t> stored(cases->begin(), cases->end());
      active_function_ir->dispatch_cases.emplace(source, std::move(stored));
      return true;
    }
    return false;
  }
  netnode wasm_node(wasmhex_node_name, 0, false);
  if ( wasm_node == BADNODE )
    return false;
  return read_dispatch_cases(wasm_node, source, cases);
}

// The faithful aggregate layouts for Go call-site pointer arguments.
// Registered in the til on first use (create_udt + set_named_type — the same
// machinery ctree_types.cpp uses for struct inference; no source-level type
// is invented: every layout mirrors the ArgInfo-proven word structure).
namespace
{
const char *kGoAggregateMemberNames[3][3] = {
  { "ptr", "len", nullptr },
  { "ptr", "len", "cap" },
  { "tab", "data", nullptr },
};

// Memoized per name: a repeated set_named_type(NTF_REPLACE) renumbers til
// ordinals and corrupts the tinfos already applied to other functions —
// the exact churn ensure_go_struct_type memoizes away (metadata.cpp, the
// -31 MERR_ARCH lesson). The aggregate display renders one such
// call per group parameter PER F5, so an unmemoized replace churned the
// til hundreds of times per sweep, order-dependently (a full corpus dump
// degrades or silently loses merged call sites depending on which
// functions decompiled first).
bool go_aggregate_type_present(const char *name, int word_count)
{
  tinfo_t existing;
  return existing.get_named_type(get_idati(), name)
    && existing.get_size() == uint64(word_count) * 8;
}

void ensure_go_aggregate_type(const char *name, int word_count, int member_set)
{
  if ( name == nullptr || word_count < 1 || word_count > 3 || member_set < 0 || member_set > 2 )
    return;
  if ( go_aggregate_type_present(name, word_count) )
    return; // already registered with this exact extent: never churn the til
  udt_type_data_t udt;
  udt.effalign = 8;
  for ( int k = 0; k < word_count; ++k )
  {
    udm_t member;
    const char *fallback[3] = { "w0", "w1", "w2" };
    const char *member_name = kGoAggregateMemberNames[member_set][k] != nullptr
      ? kGoAggregateMemberNames[member_set][k]
      : fallback[k];
    member.name = member_name;
    if ( !member.type.parse("__int64 x;", get_idati(), PT_SIL) || member.type.empty() )
      return;
    member.offset = uint64(k) * 64;
    udt.push_back(member);
  }
  udt.total_size = uint64(word_count) * 8;
  tinfo_t struct_type;
  if ( !struct_type.create_udt(udt) )
    return;
  if ( struct_type.set_named_type(get_idati(), name, NTF_REPLACE) != TERR_OK )
    return;
  tinfo_t named;
  if ( named.get_named_type(get_idati(), name) && named.get_size() != udt.total_size )
    return; // registered under our name with a different layout: do not fight it
}
} // namespace

merror_t wasmhex_filter_t::emit_dispatch_chain(codegen_t &cdg, wasm_indirect_dispatch_t &dispatch,
                                               ea_t continuation, const qvector<uint32> &cases)
{
  const size_t case_count = cases.size() / 3;
  if ( dispatch.selector.t == mop_n && dispatch.selector.size == 0 ) // defensive
    return MERR_BADARCH;
  if ( (cases.size() % 3) != 0 )
    return MERR_BADARCH;
  if ( !dispatch.tail_call && continuation == BADADDR )
    return MERR_BADARCH;
  // Locate the codegen block that owns the selector spill (sparse path);
  // reference dispatches hand the operand over directly, so the spill may
  // be missing — fall back to the block being filled (the bridge goto is
  // emitted there anyway).
  mblock_t *head = nullptr;
  for ( int m = cdg.mba->qty - 1; m >= 0 && head == nullptr; --m )
  {
    mblock_t *block = cdg.mba->get_mblock(m);
    for ( minsn_t *insn = block->head; insn != nullptr; insn = insn->next )
      if ( insn->opcode == m_mov && insn->d.t == mop_r && insn->d.r == dispatch.selector.r )
      {
        head = block;
        break;
      }
  }
  if ( head == nullptr )
    head = cdg.mb;
  if ( head == nullptr )
    return MERR_INTERR;

  const insn_t call_insn(cdg.insn);
  const bool holes = !dispatch.reference_call && case_count < dispatch.table_size;
  // Pass 1: skeleton at consecutive serials. The TEST blocks come first
  // and stay contiguous so each jz falls through to the NEXT test — the
  // exact br_table chain shape (interleaving test and case blocks
  // would collapse {fallthrough, target} onto one block). Then the
  // default, then the case bodies. All serials are known before any body
  // is filled.
  merror_t error = MERR_OK;
  int insert_at = cdg.mba->qty;
  const int chain_first = insert_at;
  const int default_serial = chain_first + int(case_count);
  // The bounds/null trap pair for holed tables is
  // part of the SAME continuous append sequence — tests, default, trap
  // pair, cases. Creating the pair in a separate second insert round
  // (after the case bodies) returned the SAME block twice on the second
  // consecutive append (null == bounds), collapsing the default block's
  // two successors into one and failing mba verification.
  const int null_serial = default_serial + 1;
  const int bounds_serial = default_serial + 2;
  const int cases_first = default_serial + (holes ? 3 : 1);
  qvector<mblock_t *> test_blocks;
  qvector<mblock_t *> case_blocks;
  test_blocks.reserve(case_count);
  case_blocks.reserve(case_count);
  mblock_t *null_block = nullptr;
  mblock_t *bounds_block = nullptr;
  for ( size_t k = 0; k < case_count; ++k )
  {
    mblock_t *test_block = cdg.mba->insert_block(insert_at++);
    if ( test_block == nullptr )
      return MERR_INTERR;
    test_block->type = BLT_2WAY;
    // 50869/50879: hand-made chain block — the engine's MBL_FAKE
    // convention for synthesized blocks (the verifier skips boundary and
    // overlap checks for fakes; these blocks legitimately share the
    // source instruction EA).
    test_block->flags |= MBL_FAKE;
    test_block->start = cdg.insn.ea;
    // 50879 fix: engine-mba blocks may not share [start,end) intervals —
    // the mba verifier (GUI sessions run it) rejects overlaps. Empty
    // intervals pass (the engine itself uses [x..x) for placeholder
    // blocks), so every hand-made chain block keeps end == start.
    test_block->end = cdg.insn.ea;
    test_blocks.push_back(test_block);
  }
  mblock_t *default_block = cdg.mba->insert_block(insert_at++);
  if ( default_block == nullptr )
    return MERR_INTERR;
  default_block->start = cdg.insn.ea;
  default_block->end = cdg.insn.ea;
  default_block->flags |= MBL_FAKE; // 50869/50879: synthesized block
  if ( holes )
  {
    null_block = cdg.mba->insert_block(insert_at++);
    if ( null_block == nullptr )
      return MERR_INTERR;
    // Touch the fresh block before the next insert: consecutive
    // insert_block calls with nothing in between return the SAME block
    // (observed live: serials 103/103). The per-iteration field stores
    // in the loops above have the same effect.
    null_block->start = cdg.insn.ea;
    null_block->end = cdg.insn.ea;
    null_block->flags |= MBL_FAKE; // 50869/50879: synthesized block
    bounds_block = cdg.mba->insert_block(insert_at++);
    if ( bounds_block == nullptr || null_block->serial != null_serial
         || bounds_block->serial != bounds_serial )
    {
      msg("WASMHEX_F5_UNSUPPORTED: dispatch trap pair insert failed at %a "
          "(serials %d/%d want %d/%d)\n",
          cdg.insn.ea, null_block ? null_block->serial : -1,
          bounds_block ? bounds_block->serial : -1, null_serial, bounds_serial);
      return MERR_INTERR;
    }
    for ( mblock_t *trap_block : { null_block, bounds_block } )
    {
      trap_block->type = mblock_type_t(0);
      trap_block->start = cdg.insn.ea;
      trap_block->end = cdg.insn.ea;
      trap_block->flags |= MBL_FAKE; // 50869/50879: synthesized block
    }
  }
  for ( size_t k = 0; k < case_count; ++k )
  {
    mblock_t *case_block = cdg.mba->insert_block(insert_at++);
    if ( case_block == nullptr )
      return MERR_INTERR;
    case_block->type = mblock_type_t(0); // terminator derived (goto EA / noret)
    case_block->start = cdg.insn.ea;
    case_block->end = cdg.insn.ea;
    case_block->flags |= MBL_FAKE; // 50869/50879: synthesized block
    case_blocks.push_back(case_block);
  }
  // Wire the tests: fallthrough to the next test (the default after the
  // last one), jump target to the matching case body. succ order matters
  // (verify builds outs as {fallthrough, target}).
  for ( size_t k = 0; k < case_count; ++k )
  {
    mblock_t *test_block = test_blocks[k];
    const uint32 slot = cases[k * 3];
    const int fallthrough = k + 1 < case_count ? test_blocks[k + 1]->serial : default_serial;
    minsn_t *jz = new minsn_t(4);
    jz->opcode = m_jz;
    jz->ea = cdg.insn.ea;
    jz->l = dispatch.selector;
    // Reference dispatches encode the table slot one past the selector
    // value (0 is the null reference) — the old compare stubs did the
    // same increment.
    const uint64 compare_value = dispatch.reference_call ? uint64(slot) + 1 : slot;
    jz->r.make_number(compare_value, dispatch.selector.size, cdg.insn.ea, 0);
    jz->d.make_blkref(cases_first + int(k));
    test_block->insert_into_block(jz, nullptr);
    test_block->succset.add_unique(fallthrough);
    test_block->succset.add_unique(cases_first + int(k));
    if ( k == 0 )
      test_block->predset.add_unique(head->serial);
    else
      test_block->predset.add_unique(test_blocks[k - 1]->serial);
    cdg.mba->get_mblock(fallthrough)->predset.add_unique(test_block->serial);
    case_blocks[k]->predset.add_unique(test_block->serial);
  }
  if ( case_count == 0 )
    default_block->predset.add_unique(head->serial);
  // Pass 2: fill the case bodies. Each body replays the abstract stack
  // into its manual block through a temporary cdg.mb/cdg.insn retarget —
  // identical discipline to the retired emit_call_dispatch_target/trap
  // stub lowerings.
  for ( size_t k = 0; k < case_count && error == MERR_OK; ++k )
  {
    const uint32 function = cases[k * 3 + 1];
    const wasmhex_call_trap_t trap = wasmhex_call_trap_t(cases[k * 3 + 2]);
    mblock_t *case_block = case_blocks[k];
    mblock_t *saved_mb = cdg.mb;
    cdg.mb = case_block;
    stack = dispatch.base_stack;
    unreachable = false;
    if ( trap != wasmhex_call_trap_t::none )
    {
      qstring helper;
      if ( dispatch.reference_call )
        helper = "wasm_trap_call_ref";
      else
        helper.sprnt("wasm_trap_call_indirect_%s",
                     trap == wasmhex_call_trap_t::null_reference ? "null" : "type");
      const uchar no_argument_types[] = { 0x7F };
      error = emit_call(cdg, helper.c_str(), 0, 0x40, false, no_argument_types, false, true);
      if ( error == MERR_OK )
      {
        stack = dispatch.base_stack;
        unreachable = true;
      }
    }
    else
    {
      for ( const wasm_value_t &argument : dispatch.arguments )
        stack.push_back(argument);
      insn_t direct_insn(call_insn);
      direct_insn.itype = wasmhex_call; // plain direct call
      direct_insn.Op1.type = o_imm;
      direct_insn.Op1.value = function;
      cdg.insn = direct_insn;
      error = dispatch.tail_call ? emit_call(cdg, nullptr, 2, 0x7F, true) : emit_call(cdg);
      cdg.insn = call_insn;
      if ( error == MERR_OK )
      {
        if ( dispatch.tail_call )
        {
          stack = dispatch.base_stack;
          unreachable = true;
        }
        else
        {
          // Move the call results into the continuation registers and
          // tail out to the continuation block (opens when decode
          // reaches it — same by-EA shape as the br_table bodies).
          if ( dispatch.continuation_stack.size()
                 != dispatch.base_stack.size() + dispatch.result_types.size()
               || stack.size() != dispatch.base_stack.size() + dispatch.result_types.size() )
          {
            error = MERR_BADARCH;
            break;
          }
          for ( size_t i = dispatch.result_types.size(); i != 0; --i )
          {
            wasm_value_t result;
            const wasm_value_t &destination
              = dispatch.continuation_stack[dispatch.base_stack.size() + i - 1];
            const uchar type = dispatch.result_types[i - 1];
            if ( destination.type != type || !pop(type, &result)
                 || !emit_call_result_move(cdg, result.op, destination.op,
                                           type == 0x7D || type == 0x7C) )
            {
              error = MERR_BADARCH;
              break;
            }
          }
          if ( error == MERR_OK )
          {
            stack = dispatch.continuation_stack;
            if ( std::none_of(resume_states.begin(), resume_states.end(),
                              [&](const wasm_branch_resume_t &item)
                              { return item.target == continuation; }) )
            {
              wasm_branch_resume_t resume;
              resume.target = continuation;
              resume.stack = dispatch.continuation_stack;
              resume_states.push_back(resume);
            }
            mop_t target;
            target.make_gvar(continuation);
            if ( cdg.emit(m_goto, &target, nullptr, nullptr) == nullptr )
              error = MERR_INTERR;
            unreachable = true;
          }
        }
      }
    }
    cdg.mb = saved_mb;
    if ( error != MERR_OK )
    {
      msg("WASMHEX_F5_UNSUPPORTED: dispatch case %u lowering failed at %a\n", unsigned(k),
          cdg.insn.ea);
      return error;
    }
  }
  if ( error != MERR_OK )
    return error;

  // Default tail: the old stream's bounds/null stub pair (sparse tables
  // with holes) or the single default trap (full tables / references).
  // The default block itself is already in place from pass 1.
  const auto emit_default_trap = [&](mblock_t *block, const char *reason)
  {
    mblock_t *saved_mb = cdg.mb;
    cdg.mb = block;
    stack = dispatch.base_stack;
    unreachable = false;
    qstring helper;
    if ( dispatch.reference_call )
      helper = "wasm_trap_call_ref";
    else
      helper.sprnt("wasm_trap_call_indirect_%s", reason);
    const uchar no_argument_types[] = { 0x7F };
    const merror_t trap_error
      = emit_call(cdg, helper.c_str(), 0, 0x40, false, no_argument_types, false, true);
    if ( trap_error == MERR_OK )
    {
      stack = dispatch.base_stack;
      unreachable = true;
    }
    cdg.mb = saved_mb;
    return trap_error;
  };
  if ( holes )
  {
    // Bounds compare then two trap blocks: out-of-table selector -> bounds
    // (jnz target), in-table hole -> null (fallthrough). Both blocks are
    // already in place from the pass 1 append sequence.
    null_block->predset.add_unique(default_block->serial);
    bounds_block->predset.add_unique(default_block->serial);
    default_block->type = BLT_2WAY;
    mblock_t *saved_mb = cdg.mb;
    cdg.mb = default_block;
    mop_t out_of_bounds, out_of_bounds32;
    if ( !make_boolean_register(cdg, &out_of_bounds)
         || !make_scratch_register(cdg, 4, 1, &out_of_bounds32) )
    {
      cdg.mb = saved_mb;
      return MERR_MEM;
    }
    mop_t last_index, zero, target;
    last_index.make_number(uint64(dispatch.table_size) - 1, dispatch.selector.size, cdg.insn.ea, 0);
    zero.make_number(0, 4, cdg.insn.ea, 0);
    target.make_blkref(bounds_block->serial);
    if ( cdg.emit(m_seta, &dispatch.selector, &last_index, &out_of_bounds) == nullptr
         || cdg.emit(m_xdu, &out_of_bounds, nullptr, &out_of_bounds32) == nullptr
         || cdg.emit(m_jnz, &out_of_bounds32, &zero, &target) == nullptr )
    {
      cdg.mb = saved_mb;
      return MERR_INTERR;
    }
    cdg.mb = saved_mb;
    default_block->succset.add_unique(null_block->serial);
    default_block->succset.add_unique(bounds_block->serial);
    if ( (error = emit_default_trap(null_block, "null")) != MERR_OK
         || (error = emit_default_trap(bounds_block, "bounds")) != MERR_OK )
      return error;
  }
  else
  {
    default_block->type = mblock_type_t(0); // no-return trap terminates it
    if ( (error = emit_default_trap(default_block, "bounds")) != MERR_OK )
      return error;
  }

  // Bridge the call-site block into the chain head through cdg and wire
  // head <-> chain-first both ways (the mba verifier expects the goto and
  // both graph edges).
  mop_t chain_entry;
  chain_entry.make_blkref(chain_first);
  if ( cdg.emit(m_goto, &chain_entry, nullptr, nullptr) == nullptr )
    return MERR_INTERR;
  head->succset.add_unique(chain_first);
  cdg.mba->get_mblock(chain_first)->predset.add_unique(head->serial);
  // Leave the post-dispatch decode state exactly as the retired
  // stub-stream lowering did.
  stack = dispatch.tail_call ? dispatch.base_stack : dispatch.continuation_stack;
  unreachable = dispatch.tail_call;
  return MERR_OK;
}

merror_t wasmhex_filter_t::emit_call(codegen_t &cdg, const char *typed_helper,
                                     int typed_helper_arguments, uchar typed_helper_type,
                                     bool tail_call, const uchar *typed_helper_arg_types,
                                     bool indirect_call, bool no_return, bool reference_call,
                                     bool unwind_call, const mop_t *const *preset_args)
{
  if ( options::debug("mc") )
  {
    char eb[128];
    int en
      = qsnprintf(eb, sizeof(eb), "M1D2_EMITCALL ea=%a helper=%s tail=%d ind=%d unw=%d ref=%d\n",
                  cdg.insn.ea, typed_helper ? typed_helper : "-", int(tail_call),
                  int(indirect_call), int(unwind_call), int(reference_call));
    if ( en > 0 )
      ::write(2, eb, size_t(en));
  }
  if ( (tail_call && typed_helper != nullptr)
       || (unwind_call
           && (tail_call || typed_helper != nullptr || indirect_call || reference_call)) )
    return MERR_BADARCH;
  // Go recognition comes from the loader's byte-level provenance
  // flag (netnode 'G' via the accessor — import signal AND global roles
  // AND PC_B majority AND frame-prologue majority). The old decompiler
  // side effect that wrote 'G' from a gojs. helper name is retired.
  if ( !go_module_recognized )
    go_module_recognized = load_go_module();
  netnode wasm_node(wasmhex_node_name, 0, false);
  ea_t unwind_catch_ea = BADADDR;
  const wasm_control_frame_t *unwind_try_frame = nullptr;
  if ( unwind_call )
  {
    if ( wasm_node == BADNODE || !get_ea_value(cdg.insn.ea, 'Y', &unwind_catch_ea)
         || unwind_catch_ea == BADADDR )
      return MERR_BADARCH;
    for ( size_t i = controls.size(); i != 0; --i )
    {
      const wasm_control_frame_t &frame = controls[i - 1];
      if ( frame.kind == 0x06 && !frame.in_catch && frame.catch_handler == unwind_catch_ea )
      {
        unwind_try_frame = &frame;
        break;
      }
    }
    if ( unwind_try_frame == nullptr )
      return MERR_BADARCH;
  }
  const uint32 type_index = indirect_call || reference_call ? uint32(cdg.insn.Op1.value) : 0;
  const uint32 table_index = indirect_call ? uint32(cdg.insn.Op2.value) : 0;
  uint64 table_minimum = 0;
  uchar table_flags = 0;
  if ( indirect_call
       && get_wasm_table_metadata(wasm_node, table_index, nullptr, &table_minimum, &table_flags)
         != wasm_table_metadata_status_t::ok )
  {
    msg("WASMHEX_DYNAMIC_CALL_UNSUPPORTED: table metadata at %a table=%u type=%u\n", cdg.insn.ea,
        table_index, type_index);
    return MERR_BADARCH;
  }
  wasmhex_call_trap_t trap_reason = wasmhex_call_trap_t::none;
  const bool statically_trapping_indirect = indirect_call && wasm_node != BADNODE
    && wasm_node.supval_ea(cdg.insn.ea, &trap_reason, sizeof(trap_reason), 'Z')
      == sizeof(trap_reason);
  if ( statically_trapping_indirect )
  {
    const char *reason = trap_reason == wasmhex_call_trap_t::bounds ? "bounds"
      : trap_reason == wasmhex_call_trap_t::null_reference          ? "null"
      : trap_reason == wasmhex_call_trap_t::type_mismatch           ? "type"
                                                                    : nullptr;
    qvector<uchar> argument_types;
    if ( reason == nullptr || !get_type_vector(wasm_node, type_index, 'A', &argument_types) )
      return MERR_BADARCH;
    argument_types.push_back((table_flags & 2) != 0 ? uchar(0x7E) : uchar(0x7F));
    if ( controls.empty() || stack.size() < controls.back().base_stack.size()
         || stack.size() - controls.back().base_stack.size() < argument_types.size() )
      return MERR_BADARCH;
    qstring helper;
    helper.sprnt("wasm_trap_call_indirect_%s_table%u_type%u", reason, table_index, type_index);
    const merror_t error = emit_call(cdg, helper.c_str(), int(argument_types.size()), 0x40, false,
                                     argument_types.begin(), false, true);
    if ( error != MERR_OK )
      return error;
    stack.resize(controls.back().base_stack.size());
    unreachable = true;
    return MERR_OK;
  }
  const auto go_pcf_it = typed_helper == nullptr && indirect_call
    ? go_pcf_call_fn_by_ea.find(cdg.insn.ea)
    : go_pcf_call_fn_by_ea.end();
  const bool go_pcf_indirect = go_pcf_it != go_pcf_call_fn_by_ea.end();
  const bool go_pcf_direct = go_pcf_indirect && go_pcf_it->second < UINT32_MAX;
  const bool go_pcf_dynamic = go_pcf_indirect && !go_pcf_direct;
  const bool go_pcf_loop_indirect
    = typed_helper == nullptr && indirect_call && cdg.insn.ea == go_pcf_loop_call_ea;
  if ( typed_helper == nullptr && indirect_call && !go_pcf_indirect && !go_pcf_loop_indirect )
  {
    // The 'j' probe is memoized on the per-function cache (the
    // per-instruction netnode read the lowering used to do).
    if ( probe_dispatch_blob(cdg.insn.ea) )
      return emit_sparse_indirect_call(cdg);
  }
  uint32 resolved_function_index = 0;
  const bool resolved_indirect = go_pcf_direct
    || (indirect_call && !go_pcf_indirect && !go_pcf_loop_indirect && wasm_node != BADNODE
        && wasm_node.supval_ea(cdg.insn.ea, &resolved_function_index,
                               sizeof(resolved_function_index), 'G')
          == sizeof(resolved_function_index));
  if ( go_pcf_direct )
    resolved_function_index = go_pcf_it->second;
  uint32 resolved_reference_index = 0;
  const bool resolved_reference = reference_call && wasm_node != BADNODE
    && wasm_node.supval_ea(cdg.insn.ea, &resolved_reference_index, sizeof(resolved_reference_index),
                           'G')
      == sizeof(resolved_reference_index);
  if ( reference_call && !resolved_reference )
    return MERR_BADARCH;
  checked_table_call_t checked_table_call = {};
  bool checked_dynamic_indirect = indirect_call && !resolved_indirect && !go_pcf_dynamic
    && wasm_node != BADNODE
    && wasm_node.supval_ea(cdg.insn.ea, &checked_table_call, sizeof(checked_table_call), 'K')
      == sizeof(checked_table_call)
    && checked_table_call.table_index == table_index && checked_table_call.type_index == type_index
    && checked_table_call.table_size != 0
    && checked_table_call.index_type == ((table_flags & 2) != 0 ? uchar(0x7E) : uchar(0x7F));
  if ( !checked_dynamic_indirect && go_pcf_loop_indirect && table_minimum != 0 )
  {
    checked_table_call.table_index = table_index;
    checked_table_call.type_index = type_index;
    checked_table_call.table_size = table_minimum;
    checked_table_call.index_type = (table_flags & 2) != 0 ? uchar(0x7E) : uchar(0x7F);
    checked_dynamic_indirect = true;
  }
  const bool dynamic_indirect
    = indirect_call && !resolved_indirect && !checked_dynamic_indirect && !go_pcf_dynamic;
  if ( cdg.insn.itype == 98 )
    if ( dynamic_indirect )
    {
      msg("WASMHEX_F5_UNSUPPORTED: call_indirect has no validated dispatch metadata at %a\n",
          cdg.insn.ea);
      return MERR_BADARCH;
    }
  const bool direct_target
    = typed_helper == nullptr && !checked_dynamic_indirect && !go_pcf_dynamic;
  const uint32 function_index = direct_target ? reference_call ? resolved_reference_index
      : resolved_indirect                                      ? resolved_function_index
                                                               : uint32(cdg.insn.Op1.value)
                                              : 0;
  const ea_t target
    = direct_target && wasm_node != BADNODE ? wasm_node.eaget_idx(function_index, 'F') : BADADDR;
  const bool imported = typed_helper != nullptr || checked_dynamic_indirect || go_pcf_dynamic
    || target == BADADDR || target == 0;
  tinfo_t callee_type;
  func_type_data_t callee_info;
  qvector<uchar> callee_param_types;
  qvector<uchar> callee_result_types;
  // Per-call synthesized argument values (aggregate-group pointers)
  // parallel to callee_param_types; an empty mop means "pop from the stack".
  qvector<mop_t> synthetic_args;
  // By-value group params (go-frame mode) — their wasm stand-in
  // type cannot match every struct extent, so the size cross-check skips
  // exactly these entries.
  std::vector<bool> go_by_value_group;
  qstring helper_name;
  if ( typed_helper != nullptr )
  {
    helper_name = typed_helper;
    qstring declaration;
    const size_t helper_length = strlen(typed_helper);
    const bool unsigned_operation
      = (helper_length >= 2 && strcmp(typed_helper + helper_length - 2, "_u") == 0)
      || (helper_length >= 7 && strcmp(typed_helper + helper_length - 7, "_u_bits") == 0)
      || (helper_length >= 10 && strcmp(typed_helper + helper_length - 10, "_u_seq_cst") == 0);
    if ( typed_helper_arg_types != nullptr )
    {
      const char *result_type = typed_helper_type == 0x40 ? "void"
        : typed_helper_type == 0x7F ? (unsigned_operation ? "unsigned int" : "int")
        : typed_helper_type == 0x7E ? (unsigned_operation ? "unsigned __int64" : "__int64")
        : typed_helper_type == 0x7D ? "float"
        : typed_helper_type == 0x7C ? "double"
        : typed_helper_type == 0x70 || typed_helper_type == 0x6F || typed_helper_type == 0x71
          || typed_helper_type == 0x72
        ? "unsigned int"
        : nullptr;
      if ( result_type == nullptr )
      {
        msg("WASMHEX_TYPED_CALL_UNSUPPORTED: result type at %a\n", cdg.insn.ea);
        return MERR_BADARCH;
      }
      declaration.sprnt("%s __usercall %s", result_type, typed_helper);
      if ( typed_helper_type == 0x7F || typed_helper_type == 0x7D || typed_helper_type == 0x70
           || typed_helper_type == 0x6F || typed_helper_type == 0x71 || typed_helper_type == 0x72 )
        declaration.append("@<r0>");
      else if ( typed_helper_type == 0x7E || typed_helper_type == 0x7C )
        declaration.append("@<r0:r1>");
      declaration.append("(");
      size_t arg_reg = 0;
      for ( int i = 0; i < typed_helper_arguments; ++i )
      {
        const uchar arg_type = typed_helper_arg_types[i];
        const char *type = arg_type == 0x7F ? "unsigned int"
          : arg_type == 0x7E                ? "unsigned __int64"
          : arg_type == 0x7D                ? "float"
          : arg_type == 0x7C                ? "double"
          : arg_type == 0x70 || arg_type == 0x6F || arg_type == 0x71 || arg_type == 0x72
          ? "unsigned int"
          : nullptr;
        const size_t arg_size = arg_type == 0x7E || arg_type == 0x7C ? 8 : 4;
        qstring argument_name;
        argument_name.sprnt("a%d", i + 1);
        if ( !append_wasm_user_argument(&declaration, type, argument_name.c_str(), arg_reg,
                                        int(arg_size), i != 0) )
        {
          msg("WASMHEX_TYPED_CALL_UNSUPPORTED: declaration argument %d at %a\n", i, cdg.insn.ea);
          return MERR_BADARCH;
        }
        arg_reg += wasm_argument_word_count(arg_reg, int(arg_size));
      }
      if ( typed_helper_arguments == 0 )
        declaration.append("void");
      declaration.append(");");
    }
    else
    {
      const char *type = typed_helper_type == 0x40 ? "void"
        : typed_helper_type == 0x7E ? (unsigned_operation ? "unsigned __int64" : "__int64")
        : typed_helper_type == 0x7F ? (unsigned_operation ? "unsigned int" : "int")
        : typed_helper_type == 0x70 || typed_helper_type == 0x6F || typed_helper_type == 0x71
          || typed_helper_type == 0x72
        ? "unsigned int"
        : nullptr;
      if ( type == nullptr )
        return MERR_BADARCH;
      // The fallback declaration must follow the helper sign suffix
      // like the typed-argument path does — wasm_i32_div_u/rem_u and the
      // i64 variants are unsigned operations and have to produce an
      // unsigned result (and unsigned argument declaration), otherwise
      // Hex-Rays starts the lvar sign from an int helper return.
      const char *i32_t = unsigned_operation ? "unsigned int" : "int";
      const char *i64_t = unsigned_operation ? "unsigned __int64" : "__int64";
      if ( typed_helper_arguments == 3 && typed_helper_type == 0x40 )
        declaration.sprnt(
          "void __usercall %s(unsigned int a1@<r0>, unsigned int a2@<r1>, unsigned int a3@<r2>);",
          typed_helper);
      else if ( typed_helper_arguments == 1 && typed_helper_type == 0x7F )
        declaration.sprnt("%s __usercall %s@<r0>(%s a1@<r0>);", i32_t, typed_helper, i32_t);
      else if ( typed_helper_arguments == 1 && typed_helper_type == 0x7E )
        declaration.sprnt("%s __usercall %s@<r0:r1>(%s a1@<r0:r1>);", i64_t, typed_helper, i64_t);
      else if ( typed_helper_arguments == 2 && typed_helper_type == 0x7F )
        declaration.sprnt("%s __usercall %s@<r0>(%s a1@<r0>, %s a2@<r1>);", i32_t, typed_helper,
                          i32_t, i32_t);
      else if ( typed_helper_arguments == 2 && typed_helper_type == 0x7E )
        declaration.sprnt("%s __usercall %s@<r0:r1>(%s a1@<r0:r1>, %s a2@<r2:r3>);", i64_t,
                          typed_helper, i64_t, i64_t);
      else
        return MERR_BADARCH;
    }
    if ( !parse_decl(&callee_type, nullptr, get_idati(), declaration.c_str(),
                     PT_SIL | PT_SEMICOLON) )
    {
      msg("WASMHEX_TYPED_CALL_UNSUPPORTED: declaration \"%s\" at %a\n", declaration.c_str(),
          cdg.insn.ea);
      return MERR_BADARCH;
    }
    if ( preset_args != nullptr )
    {
      synthetic_args.resize(size_t(typed_helper_arguments));
      for ( int i = 0; i < typed_helper_arguments; ++i )
      {
        if ( preset_args[i] != nullptr )
          synthetic_args[size_t(i)] = *preset_args[i];
        if ( typed_helper_arg_types != nullptr )
          callee_param_types.push_back(typed_helper_arg_types[i]);
      }
    }
  }
  else if ( checked_dynamic_indirect )
  {
    qvector<uchar> signature_params;
    if ( wasm_node == BADNODE || !get_type_vector(wasm_node, type_index, 'A', &signature_params)
         || !get_type_vector(wasm_node, type_index, 'B', &callee_result_types)
         || !make_wasm_function_type(signature_params, callee_result_types, false, &callee_type) )
    {
      msg("WASMHEX_DYNAMIC_CALL_UNSUPPORTED: signature at %a table=%u type=%u\n", cdg.insn.ea,
          table_index, type_index);
      return MERR_BADARCH;
    }
    callee_param_types = signature_params;
  }
  else if ( const auto go_shape = go_call_shapes.find(cdg.insn.ea);
            go_shape != go_call_shapes.end() && (function_index < UINT32_MAX || go_pcf_dynamic) )
  {
    const bool result_types_ok = wasm_node != BADNODE
      && (go_pcf_dynamic ? get_type_vector(wasm_node, type_index, 'B', &callee_result_types)
                         : get_type_vector(wasm_node, function_index, 'Q', &callee_result_types));
    if ( !result_types_ok || callee_result_types.size() > 1 )
    {
      msg("WASMHEX_F5_UNSUPPORTED: G7 shape results at %a ok=%d n=%zu\n", cdg.insn.ea,
          int(result_types_ok), callee_result_types.size());
      return MERR_BADARCH;
    }
    const char *result_decl = callee_result_types.empty() ? "void"
      : callee_result_types[0] == 0x7E                    ? "__int64"
      : callee_result_types[0] == 0x7D                    ? "float"
      : callee_result_types[0] == 0x7C                    ? "double"
                                                          : "int";
    qstring declaration;
    declaration.sprnt("%s __usercall dummy", result_decl);
    if ( !callee_result_types.empty() )
    {
      const size_t result_size
        = callee_result_types[0] == 0x7E || callee_result_types[0] == 0x7C ? 8 : 4;
      declaration.append(result_size == 8 ? "@<r0:r1>" : "@<r0>");
    }
    declaration.append("(");
    size_t word = 0;
    bool declaration_ok = true;
    // A proven EMPTY variadic []any drops its parameter — the
    // call renders f(a, b) exactly like Go source with no variadic args.
    const size_t shape_arg_count = go_shape->second.params.size()
      - (go_shape->second.empty_variadic_args && !go_shape->second.params.empty() ? 1 : 0);
    for ( size_t i = 0; i < shape_arg_count && declaration_ok; ++i )
    {
      // Local copy: a failed named-group registration downgrades THIS
      // argument's rendering without mutating the collected shape (the
      // next F5 retries the named path).
      go_call_param_t param = go_shape->second.params[i];
      qstring name;
      name.sprnt("a%zu", i + 1);
      if ( param.is_group )
      {
        if ( !param.group_fields.empty()
             && !ensure_go_struct_type(param.group_type.c_str(), param.group_fields, param.bytes) )
        {
          param.group_fields.clear();
          if ( param.bytes == 24 )
            param.group_type = "go_slice";
          else
            param.group_type.sprnt("go_agg%u", unsigned(param.bytes));
        }
        if ( param.group_fields.empty() )
        {
          ensure_go_aggregate_type(param.group_type.c_str(), int(param.bytes / 8),
                                   param.bytes == 24     ? 1
                                     : param.bytes == 16 ? 2
                                                         : 0);
        }
        if ( go_frame_mode )
        {
          // By-value stack argument at the group's window offset.
          qstring by_value;
          by_value.sprnt("%s %s@<0:^%u.%u>", param.group_type.c_str(), name.c_str(),
                         unsigned(param.offset), unsigned(param.bytes));
          if ( i != 0 )
            declaration.append(", ");
          declaration.append(by_value);
          word += size_t(param.bytes / 8);
          continue;
        }
        qstring pointer_decl;
        pointer_decl.sprnt("%s *", param.group_type.c_str());
        if ( !append_wasm_user_argument(&declaration, pointer_decl.c_str(), name.c_str(), word, 4,
                                        i != 0) )
        {
          msg("WASMHEX_F5_UNSUPPORTED: G7 shape append-ptr at %a word=%zu\n", cdg.insn.ea, word);
          declaration_ok = false;
          break;
        }
        word += 1;
      }
      else if ( go_frame_mode )
      {
        const uchar type = param.type;
        const char *type_decl = type == 0x7E ? "__int64"
          : type == 0x7D                     ? "float"
          : type == 0x7C                     ? "double"
                                             : "int";
        const int size = (type == 0x7E || type == 0x7C) ? 8 : 4;
        qstring by_value;
        by_value.sprnt("%s %s@<0:^%u.%d>", type_decl, name.c_str(), unsigned(param.offset), size);
        if ( i != 0 )
          declaration.append(", ");
        declaration.append(by_value);
        word += size_t(size) / 4;
      }
      else
      {
        const uchar type = param.type;
        const char *type_decl = type == 0x7E ? "__int64"
          : type == 0x7D                     ? "float"
          : type == 0x7C                     ? "double"
                                             : "int";
        const int size = (type == 0x7E || type == 0x7C) ? 8 : 4;
        if ( !append_wasm_user_argument(&declaration, type_decl, name.c_str(), word, size, i != 0) )
        {
          declaration_ok = false;
          break;
        }
        word += size_t(size) / 4;
      }
    }
    if ( !declaration_ok )
      return MERR_BADARCH;
    if ( go_shape->second.params.empty() )
      declaration.append("void");
    declaration.append(");");
    if ( !parse_decl(&callee_type, nullptr, get_idati(), declaration.c_str(),
                     PT_SIL | PT_SEMICOLON) )
    {
      msg("WASMHEX_F5_UNSUPPORTED: Go aggregate call signature \"%s\" at %a\n", declaration.c_str(),
          cdg.insn.ea);
      if ( options::debug("mc") )
      {
        std::string line("M1D2_PARSE_FAIL ");
        line += declaration.c_str();
        line += "\n";
        ::write(2, line.data(), line.size());
      }
      return MERR_BADARCH;
    }
    callee_param_types.clear();
    go_by_value_group.assign(shape_arg_count, false);
    for ( size_t i = 0; i < shape_arg_count; ++i )
    {
      const go_call_param_t &param = go_shape->second.params[i];
      // 0x7B (v128, 16 bytes) stands in for a by-value group in the
      // wasm-type arity/size cross-check below (16 == the struct extent).
      if ( param.is_group && go_frame_mode )
      {
        callee_param_types.push_back(uchar(0x7B));
        go_by_value_group[i] = true;
      }
      else
        callee_param_types.push_back(param.is_group ? uchar(0x7E) : param.type);
    }
    if ( go_frame_mode )
    {
      // Replay the consumed window stores into the caller-frame
      // window cells (stack vars after the frame mapping), then reference
      // each argument's cell as the by-value mop. Words pop in reverse
      // store order (the abstract stack is LIFO).
      if ( !tail_call )
      {
        for ( size_t i = shape_arg_count; i != 0; --i )
        {
          const go_call_param_t &param = go_shape->second.params[i - 1];
          // Group stores were NOT consumed (go_call_group_stores) — their
          // words are already in the window cells as real stores. Only
          // scalar stores kept their values on the abstract stack.
          if ( param.is_group )
            continue;
          if ( stack.empty() )
            return MERR_BADARCH;
          wasm_value_t value = stack.back();
          stack.pop_back();
          const int scalar_size = (param.type == 0x7E || param.type == 0x7C) ? 8 : 4;
          // The kept value may be a virtual frame-address
          // marker (a Go `&stack_cell` argument word kept by the consumed
          // store) — its op is empty (mop_z) and emit_move would fail.
          // Materialize (&stkvar or the raw sp+k linear address) and widen
          // to the scalar width, the same rule every other marker sink
          // applies.
          if ( is_c_frame_marker(value) )
          {
            if ( !materialize_c_frame_marker(cdg, &value) )
            {
              mop_t raw_address;
              if ( !lower_c_frame_marker_address(cdg, value, stack.size(), &raw_address) )
                return reject_c_frame_marker_use(cdg, "by-value window arg out of frame");
              value.op = raw_address;
            }
            if ( !widen_materialized_address(cdg, &value.op, scalar_size, 2) )
              return MERR_MEM;
          }
          const size_t cell_off = size_t(param.offset);
          if ( cell_off + size_t(scalar_size) > size_t(cdg.mba->frsize) )
            return MERR_BADARCH;
          mop_t cell;
          cell.make_stkvar(cdg.mba, sval_t(cell_off));
          cell.size = scalar_size;
          if ( !emit_move(cdg, value.op, cell, value.type == 0x7D || value.type == 0x7C) )
          {
            if ( options::debug("mc") )
            {
              char rb[144];
              int rn = qsnprintf(
                rb, sizeof(rb), "M1D2_REPLAY_FAIL ea=%a i=%d vsize=%d vt=%d cell=%d\n", cdg.insn.ea,
                int(i - 1), int(value.op.size), int(value.op.t), int(cell.size));
              if ( rn > 0 )
                ::write(2, rb, size_t(rn));
            }
            return MERR_INTERR;
          }
        }
      }
      synthetic_args.resize(shape_arg_count);
      for ( size_t i = 0; i < shape_arg_count; ++i )
      {
        const go_call_param_t &param = go_shape->second.params[i];
        const size_t bytes = param.is_group ? size_t(param.bytes)
                                            : ((param.type == 0x7E || param.type == 0x7C) ? 8 : 4);
        if ( param.fold_global_source )
        {
          // The receiver cells were proven to be one write (a
          // constant-address 16-byte load pair) + one read (this call) —
          // the argument IS the global itself, not the window cell.
          mop_t global;
          global.make_gvar(ea_t(wasmhex_memory_base + param.fold_global_address));
          global.size = int(bytes);
          synthetic_args[i] = global;
          continue;
        }
        // fold_const_pair (and the unfolded groups) keep the cell mop: the
        // consumed constant stores leave it dead and the literal swap
        // (or the kept stores) render the value.
        mop_t cell;
        cell.make_stkvar(cdg.mba, sval_t(param.offset));
        cell.size = int(bytes);
        synthetic_args[i] = cell;
      }
    }
    else
    {
      // Group pointer values are synthesized from the call's SP-cache local
      // (the window base): linear = xdu(SP + group offset) + memory base —
      // the same address shape the visible window stores display.
      if ( go_shape->second.sp_local >= locals.size()
           || locals[go_shape->second.sp_local].storage.empty() )
      {
        msg("WASMHEX_F5_UNSUPPORTED: Go aggregate SP local unavailable at %a\n", cdg.insn.ea);
        return MERR_BADARCH;
      }
      const mop_t &sp_base = locals[go_shape->second.sp_local].storage;
      int synthetic_scratch = 0;
      for ( size_t i = 0; i < shape_arg_count; ++i )
      {
        const go_call_param_t &param = go_shape->second.params[i];
        if ( !param.is_group )
          continue;
        mop_t wide;
        if ( !make_scratch_register(cdg, 8, synthetic_scratch++, &wide) )
          return MERR_MEM;
        if ( cdg.emit(m_xdu, &sp_base, nullptr, &wide) == nullptr )
          return MERR_INTERR;
        // One folded addend — linear base + window offset — the same shape
        // make_effective_memory_address produces for dynamic addresses.
        // With linear memory at EA 0 the base term is zero; only a
        // nonzero window offset still adds, so the pointer value shown is
        // the program's own wasm32 value.
        const uint64 addend_value = uint64(wasmhex_memory_base) + param.offset;
        if ( addend_value != 0 )
        {
          mop_t addend;
          addend.make_number(addend_value, 8, cdg.insn.ea, 0);
          if ( cdg.emit(m_add, &wide, &addend, &wide) == nullptr )
            return MERR_INTERR;
        }
        synthetic_args.resize(std::max(synthetic_args.size(), i + 1));
        synthetic_args[i] = wide;
      }
    }
  }
  else if ( const auto go_args = go_call_args_by_call.find(cdg.insn.ea);
            go_args != go_call_args_by_call.end()
            && (function_index < UINT32_MAX || go_pcf_dynamic) )
  {
    // A recorded Go call with consumed argument stores — build the
    // rendered signature from the Go argument types (blob 'x' v2/v3) plus
    // the callee's wasm result type, so the call shows f(a, b) instead
    // of the raw wasm (PC_B i32) signature. Zero-argument calls travel
    // this road too (the old !empty() guard dropped them back onto the
    // raw PC_B signature path).
    const bool result_types_ok = wasm_node != BADNODE
      && (go_pcf_dynamic ? get_type_vector(wasm_node, type_index, 'B', &callee_result_types)
                         : get_type_vector(wasm_node, function_index, 'Q', &callee_result_types));
    if ( !result_types_ok || callee_result_types.size() > 1 )
    {
      msg("WASMHEX_F5_UNSUPPORTED: G7 shape results at %a ok=%d n=%zu\n", cdg.insn.ea,
          int(result_types_ok), callee_result_types.size());
      return MERR_BADARCH;
    }
    const char *result_decl = callee_result_types.empty() ? "void"
      : callee_result_types[0] == 0x7E                    ? "__int64"
      : callee_result_types[0] == 0x7D                    ? "float"
      : callee_result_types[0] == 0x7C                    ? "double"
                                                          : "int";
    qstring declaration;
    declaration.sprnt("%s __usercall dummy", result_decl);
    if ( !callee_result_types.empty() )
    {
      const size_t result_size
        = callee_result_types[0] == 0x7E || callee_result_types[0] == 0x7C ? 8 : 4;
      declaration.append(result_size == 8 ? "@<r0:r1>" : "@<r0>");
    }
    declaration.append("(");
    size_t word = 0;
    for ( size_t i = 0; i < go_args->second.size(); ++i )
    {
      const uchar type = go_args->second[i];
      const char *type_decl = type == 0x7E ? "__int64"
        : type == 0x7D                     ? "float"
        : type == 0x7C                     ? "double"
                                           : "int";
      qstring name;
      name.sprnt("a%zu", i + 1);
      const int size = (type == 0x7E || type == 0x7C) ? 8 : 4;
      if ( !append_wasm_user_argument(&declaration, type_decl, name.c_str(), word, size, i != 0) )
        return MERR_BADARCH;
      word += size_t(size) / 4;
    }
    if ( go_args->second.empty() )
      declaration.append("void");
    declaration.append(");");
    if ( !parse_decl(&callee_type, nullptr, get_idati(), declaration.c_str(),
                     PT_SIL | PT_SEMICOLON) )
    {
      msg("WASMHEX_F5_UNSUPPORTED: Go call signature at %a\\n", cdg.insn.ea);
      return MERR_BADARCH;
    }
    callee_param_types = go_args->second;
    // direct_target already resolved `target` from 'F' for a direct call;
    // a Go callee is always a module function (imported would have meant
    // a missing 'F' entry, which direct_target already encoded).
  }
  else
  {
    if ( wasm_node == BADNODE
         || !get_type_vector(wasm_node, function_index, 'P', &callee_param_types)
         || !get_type_vector(wasm_node, function_index, 'Q', &callee_result_types) )
      return MERR_BADARCH;
    if ( imported )
    {
      if ( wasm_node.supstr(&helper_name, function_index, 'H') <= 0 )
        return MERR_BADARCH;
    }
    if ( !make_wasm_function_type(callee_param_types, callee_result_types, false, &callee_type) )
      return MERR_BADARCH;
  }
  size_t synthetic_count = 0;
  for ( const mop_t &synthetic : synthetic_args )
    if ( !synthetic.empty() )
      ++synthetic_count;
  if ( options::debug("mc") && go_frame_mode && go_call_shapes.count(cdg.insn.ea) != 0 )
  {
    char buf[128];
    int n = qsnprintf(buf, sizeof(buf), "M1D2_CALL_POST ea=%a params=%d synth=%d stksz=%d\n",
                      cdg.insn.ea, int(callee_param_types.size()), int(synthetic_count),
                      int(stack.size()));
    if ( n > 0 )
      ::write(2, buf, size_t(n));
  }
  if ( !callee_type.get_func_details(&callee_info, GTD_CALC_ARGLOCS)
       || callee_param_types.size() - synthetic_count > stack.size() )
  {
    if ( options::debug("mc") )
    {
      char buf[160];
      int n = qsnprintf(buf, sizeof(buf), "M1D2_SIGCHECK_FAIL ea=%a params=%d synth=%d stksz=%d\n",
                        cdg.insn.ea, int(callee_param_types.size()), int(synthetic_count),
                        int(stack.size()));
      if ( n > 0 )
        ::write(2, buf, size_t(n));
    }
    msg("WASMHEX_F5_UNSUPPORTED: signature/stack (%d params, %d synthetic, %d stack) at %a\n",
        int(callee_param_types.size()), int(synthetic_count), int(stack.size()), cdg.insn.ea);
    return MERR_BADARCH;
  }
  const bool tuple_sret = typed_helper == nullptr && callee_result_types.size() > 1
    && wasm_results_size(callee_result_types) > 16;
  if ( typed_helper == nullptr )
  {
    if ( options::debug("mc")
         && callee_param_types.size() + size_t(tuple_sret) != callee_info.size() )
    {
      char buf[128];
      int n
        = qsnprintf(buf, sizeof(buf), "M1D2_ARITY_FAIL ea=%a want=%d got=%d\n", cdg.insn.ea,
                    int(callee_param_types.size() + size_t(tuple_sret)), int(callee_info.size()));
      if ( n > 0 )
        ::write(2, buf, size_t(n));
    }
    if ( callee_param_types.size() + size_t(tuple_sret) != callee_info.size()
         || (tuple_sret ? !callee_info.rettype.is_void()
                        : callee_result_types.empty() != callee_info.rettype.is_void()) )
      return MERR_BADARCH;
    for ( size_t i = 0; i < callee_param_types.size(); ++i )
    {
      // By-value group args have no faithful wasm stand-in type
      // the declaration itself carried the extent.
      if ( i < go_by_value_group.size() && go_by_value_group[i] )
        continue;
      if ( options::debug("mc")
           && wasm_type_size(callee_param_types[i]) != callee_info[i].type.get_size() )
      {
        char buf[160];
        int n = qsnprintf(buf, sizeof(buf), "M1D2_SIZE_FAIL ea=%a i=%d want=%d got=%d\n",
                          cdg.insn.ea, int(i), int(wasm_type_size(callee_param_types[i])),
                          int(callee_info[i].type.get_size()));
        if ( n > 0 )
          ::write(2, buf, size_t(n));
      }
      if ( wasm_type_size(callee_param_types[i]) == 0
           || wasm_type_size(callee_param_types[i]) != callee_info[i].type.get_size() )
        return MERR_BADARCH;
    }
    if ( callee_result_types.size() > 1 )
    {
      size_t words = 0;
      if ( !multi_result_word_count(callee_result_types, &words, tuple_sret)
           || (tuple_sret ? !callee_info[callee_param_types.size()].type.is_ptr()
                          : !callee_info.rettype.is_udt())
           || (tuple_sret && callee_info[callee_param_types.size()].type.get_size() != 4) )
        return MERR_BADARCH;
    }
    else if ( !callee_result_types.empty()
              && wasm_type_size(callee_result_types[0]) != callee_info.rettype.get_size() )
      return MERR_BADARCH;
  }
  else
  {
    // Preset calls pre-filled callee_param_types above — fill only
    // when absent.
    if ( callee_param_types.empty() )
      for ( size_t i = 0; i < callee_info.size(); ++i )
        callee_param_types.push_back(typed_helper_arg_types == nullptr
                                       ? get_wasm_type(callee_info[i].type)
                                       : typed_helper_arg_types[i]);
    if ( typed_helper_type != 0x40 )
      callee_result_types.push_back(typed_helper_type);
    if ( callee_param_types.size() - synthetic_count > stack.size() )
    {
      msg("WASMHEX_TYPED_CALL_UNSUPPORTED: pop arity at %a\n", cdg.insn.ea);
      return MERR_BADARCH;
    }
  }
  if ( tail_call )
  {
    if ( typed_helper != nullptr || function_results != callee_result_types
         || function_type.get_cc() != callee_info.get_cc()
         || compare_arglocs(function_type.retloc, callee_info.retloc) != 0 )
      return MERR_BADARCH;
  }

  qvector<wasm_value_t> args;
  args.resize(callee_param_types.size());
  wasm_value_t checked_table_index;
  wasm_value_t checked_lookup;
  wasm_value_t go_pcf_target;
  if ( reference_call )
  {
    wasm_value_t reference;
    if ( !pop(0x70, &reference) || reference.function_ref_index != function_index )
      return MERR_BADARCH;
  }
  if ( checked_dynamic_indirect )
  {
    if ( !pop(checked_table_call.index_type, &checked_table_index) )
    {
      msg("WASMHEX_DYNAMIC_CALL_UNSUPPORTED: selector pop at %a type=%u\n", cdg.insn.ea,
          checked_table_call.index_type);
      return MERR_BADARCH;
    }
    // Run the table lookup while the arguments are still on the
    // abstract stack. Operand registers are keyed by (depth, type), so
    // with the selector on top the helper's result materializes in the
    // selector's own slot and can never land on an argument. The retired
    // order (lookup after popping the arguments) wrote the result into
    // args[0]'s register inside emit_call and only then "detected" the
    // conflict — copying the already-clobbered value to a scratch reg.
    push(checked_table_index.op, checked_table_call.index_type);
    // Display rename: the canonical table0/type0 lookup (every
    // single-table corpus) is just `wasm_table_lookup`; multi-table /
    // multi-signature modules append the distinguishing indices so each
    // typed helper stays unique.
    qstring lookup_name;
    if ( table_index == 0 && type_index == 0 )
      lookup_name = "wasm_table_lookup";
    else
      lookup_name.sprnt("wasm_table_lookup_t%u_ty%u", table_index, type_index);
    const uchar lookup_arg_type[] = { checked_table_call.index_type };
    const merror_t lookup_error
      = emit_call(cdg, lookup_name.c_str(), 1, 0x7F, false, lookup_arg_type);
    if ( lookup_error != MERR_OK )
      return lookup_error;
    if ( !pop(0x7F, &checked_lookup) )
      return MERR_BADARCH;
    for ( size_t i = callee_param_types.size(); i != 0; --i )
    {
      const uchar param_type = callee_param_types[i - 1];
      if ( !pop(param_type, &args[i - 1], has_c_frame && param_type == 0x7F && !tail_call) )
      {
        msg("WASMHEX_DYNAMIC_CALL_UNSUPPORTED: argument pop %u at %a type=%u\n", uint32(i - 1),
            cdg.insn.ea, param_type);
        return MERR_BADARCH;
      }
    }
  }
  else
  {
    if ( go_pcf_indirect )
    {
      if ( !pop(0x7E, &go_pcf_target) )
        return MERR_BADARCH;
    }
    else if ( indirect_call )
    {
      wasm_value_t element_index;
      const uchar element_index_type = (table_flags & 2) != 0 ? uchar(0x7E) : uchar(0x7F);
      if ( !pop(element_index_type, &element_index) )
        return MERR_BADARCH;
    }
    const bool allow_marker_args = has_c_frame && !tail_call
      && (typed_helper == nullptr
          || classify_typed_helper(typed_helper) == wasm_helper_effect_t::memory_or_global);
    for ( size_t i = callee_param_types.size(); i != 0; --i )
    {
      const uchar param_type = callee_param_types[i - 1];
      if ( i - 1 < synthetic_args.size() && !synthetic_args[i - 1].empty() )
      {
        // An aggregate-group pointer — synthesized above, not on the
        // abstract stack (the group's words rendered as real stores).
        args[i - 1].op = synthetic_args[i - 1];
        args[i - 1].type = param_type;
        continue;
      }
      // Go word arguments may be frame-address markers (&stack_cell
      // escape) at i64 width too — the marker materializes inside the
      // callinfo args loop below.
      if ( !pop(param_type, &args[i - 1],
                allow_marker_args && (param_type == 0x7F || go_frame_mode)) )
        return MERR_BADARCH;
    }
  }

  wasm_value_t unwind_payload;
  const bool has_unwind_payload = unwind_call && unwind_try_frame->catch_payload_type != 0x40;
  if ( unwind_call )
  {
    if ( unwind_try_frame->catch_payload_type == 0x40 )
    {
      if ( !callee_param_types.empty() )
        return MERR_BADARCH;
    }
    else if ( has_unwind_payload )
    {
      const uchar payload_type = unwind_try_frame->catch_payload_type;
      if ( args.size() == 1 && is_c_frame_marker(args[0]) )
        return reject_c_frame_marker_use(cdg, "unwind call payload");
      if ( (payload_type != 0x7F && payload_type != 0x7E && payload_type != 0x7D
            && payload_type != 0x7C)
           || callee_param_types.size() != 1 || callee_param_types[0] != payload_type
           || args.size() != 1 || args[0].type != payload_type
           || !stack_has_prefix(unwind_try_frame->base_stack)
           || stack.size() != unwind_try_frame->base_stack.size()
           || !make_operand_register(cdg, payload_type, unwind_try_frame->base_stack.size(),
                                     &unwind_payload.op)
           || !emit_move(cdg, args[0].op, unwind_payload.op,
                         payload_type == 0x7D || payload_type == 0x7C) )
        return MERR_BADARCH;
      unwind_payload.type = payload_type;
    }
    else
    {
      return MERR_BADARCH;
    }
    if ( !stack_has_prefix(unwind_try_frame->base_stack)
         || stack.size() != unwind_try_frame->base_stack.size() )
      return MERR_BADARCH;
  }

  const bool emit_as_icall = checked_dynamic_indirect || go_pcf_dynamic;
  mop_t indirect_callee;
  if ( checked_dynamic_indirect )
  {
    indirect_callee = checked_lookup.op; // Resolved at the selector's depth
    // On a wide (64-bit program) database the m_icall callee operand
    // must be pointer-sized — the 4-byte table-lookup result as a
    // callee fails verification (imported-table dispatch). Zero-extend once.
    if ( inf_is_64bit() && indirect_callee.size == 4 )
    {
      mop_t wide_callee;
      if ( !make_scratch_register(cdg, 8, 0, &wide_callee) )
        return MERR_MEM;
      if ( cdg.emit(m_xdu, &indirect_callee, nullptr, &wide_callee) == nullptr )
        return MERR_INTERR;
      indirect_callee = wide_callee;
    }
  }
  else if ( go_pcf_dynamic )
  {
    indirect_callee = go_pcf_target.op;
    if ( !inf_is_64bit() && indirect_callee.size == 8 )
    {
      mop_t narrow_callee;
      if ( !make_scratch_register(cdg, 4, 0, &narrow_callee) )
        return MERR_MEM;
      if ( cdg.emit(m_low, &indirect_callee, nullptr, &narrow_callee) == nullptr )
        return MERR_INTERR;
      indirect_callee = narrow_callee;
    }
  }

  mcallinfo_t *callinfo = new mcallinfo_t(imported ? BADADDR : target, int(callee_info.size()));
  if ( options::debug("mc") )
  {
    char buf[192];
    int n = qsnprintf(buf, sizeof(buf),
                      "M1D2_CALLINFO ea=%a tail=%d unwind=%d ptypes=%d info=%d shape=%d g4=%d\n",
                      cdg.insn.ea, int(tail_call), int(unwind_call), int(callee_param_types.size()),
                      int(callee_info.size()), int(go_call_shapes.count(cdg.insn.ea)),
                      int(go_call_args_by_call.count(cdg.insn.ea)));
    if ( n > 0 )
      ::write(2, buf, size_t(n));
  }
  if ( !callinfo->set_type(callee_type) )
  {
    delete callinfo;
    return MERR_BADARCH;
  }
  for ( size_t i = 0; i < callinfo->args.size(); ++i )
    if ( callinfo->args[i].argloc.is_badloc() )
    {
      msg("WASMHEX_CALLINFO_ARGLOC_BAD: arg %u type=%d at %a\n", unsigned(i),
          int(callinfo->args[i].argloc.atype()), cdg.insn.ea);
      delete callinfo;
      return MERR_BADARCH;
    }
  callinfo->args.resize(callee_info.size());
  for ( size_t i = 0; i < callee_param_types.size(); ++i )
  {
    mcallarg_t &arg = callinfo->args[i];
    if ( is_c_frame_marker(args[i]) && args[i].op.t == mop_z && !tail_call )
    {
      const int64 marker_k = int64(args[i].c_frame_offset)
        + (args[i].is_c_sp_base && c_frame_metadata.direction < 0
             ? int64(c_frame_metadata.c_frame_bytes)
             : 0);
      if ( has_c_frame && !args[i].is_c_sp_base && marker_k >= 0
           && marker_k < int64(c_frame_metadata.c_frame_bytes)
           && marker_k < int64(cdg.mba->frsize) )
      {
        mop_t cell;
        cell.make_stkvar(cdg.mba, sval_t(marker_k));
        cell.size = NOSIZE;
        mop_addr_t *cell_address = new mop_addr_t(cell, NOSIZE, NOSIZE);
        if ( cell_address == nullptr )
        {
          delete callinfo;
          return MERR_MEM;
        }
        args[i].op.t = mop_a;
        args[i].op.a = cell_address;
        args[i].op.size = 4;
      }
      else
      {
        mop_t raw;
        if ( !has_c_frame || !lower_c_frame_marker_address(cdg, args[i], stack.size(), &raw) )
        {
          msg("WASMHEX_F5_UNSUPPORTED: empty frame-marker call argument %u at %a\n", unsigned(i),
              cdg.insn.ea);
          delete callinfo;
          return MERR_BADARCH;
        }
        args[i].op = raw;
      }
    }
    if ( options::debug("mc") && !args[i].op.empty()
         && int(callee_info[i].type.get_size()) != args[i].op.size )
    {
      char buf[176];
      int n = qsnprintf(buf, sizeof(buf),
                        "M1D2_ARGSIZE ea=%a i=%d mop_t=%d mop_size=%d type_size=%d stksz=%d\n",
                        cdg.insn.ea, int(i), int(args[i].op.t), int(args[i].op.size),
                        int(callee_info[i].type.get_size()), int(stack.size()));
      if ( n > 0 )
        ::write(2, buf, size_t(n));
    }
    if ( is_c_frame_marker(args[i]) )
    {
      const int64 k = int64(args[i].c_frame_offset);
      const bool in_frame = has_c_frame && args[i].is_c_frame_address && !args[i].is_c_sp_base
        && k >= 0 && k < int64(c_frame_metadata.c_frame_bytes) && k < int64(cdg.mba->frsize);
      if ( in_frame )
      {
        mop_t stkvar;
        stkvar.make_stkvar(cdg.mba, sval_t(k));
        stkvar.size = NOSIZE;
        mop_addr_t *address = new mop_addr_t(stkvar, NOSIZE, NOSIZE);
        if ( address == nullptr )
        {
          delete callinfo;
          return MERR_MEM;
        }
        mop_t pointer_mop;
        pointer_mop.t = mop_a;
        pointer_mop.a = address;
        pointer_mop.size = 4;
        // An 8-byte declared argument type (Go word args are
        // __int64) with a 4-byte wasm32 address mop is an argument
        // size/type mismatch at verify — widen once.
        if ( int(callee_info[i].type.get_size()) == 8 )
        {
          mop_t wide;
          if ( !make_scratch_register(cdg, 8, 3, &wide) )
          {
            delete callinfo;
            return MERR_MEM;
          }
          if ( cdg.emit(m_xdu, &pointer_mop, nullptr, &wide) == nullptr )
          {
            delete callinfo;
            return MERR_INTERR;
          }
          pointer_mop = wide;
        }
        arg.copy_mop(pointer_mop);
      }
      else
      {
        // C-frame fix: C-ABI call arguments legitimately live past the
        // frame end (caller pushes them at sp+framesize+8+off) — resolve
        // to the real linear address sp_global + offset.
        mop_t raw;
        if ( !args[i].op.empty() )
        {
          // The empty-mop fix above already materialized
          // (and emitted) this address — reuse it instead of emitting
          // the sp_global computation twice.
          raw = args[i].op;
        }
        else if ( !has_c_frame || !args[i].is_c_frame_address
                  || !lower_c_frame_marker_address(cdg, args[i], stack.size(), &raw) )
        {
          msg("WASMHEX_F5_UNSUPPORTED: C shadow stack call argument offset %lld (frame=%u) "
              "out of bounds at %a\n",
              static_cast<long long>(k), unsigned(c_frame_metadata.c_frame_bytes), cdg.insn.ea);
          delete callinfo;
          return MERR_BADARCH;
        }
        // Widen the 4-byte raw sp+k address to the
        // declared argument width (Go word args are __int64) — the same
        // one-time xdu the in-frame branch applies.
        if ( int(callee_info[i].type.get_size()) == 8
             && !widen_materialized_address(cdg, &raw, 8, 3) )
        {
          delete callinfo;
          return MERR_MEM;
        }
        arg.copy_mop(raw);
      }
    }
    else if ( is_table_base_marker(args[i]) )
    {
      const ea_t target_ea = wasm_node.eaget_idx(args[i].table_base_offset, 'E');
      if ( target_ea == BADADDR || target_ea == 0 )
      {
        msg("WASMHEX_F5_UNSUPPORTED: invalid table_base element slot %u at %a\n",
            args[i].table_base_offset, cdg.insn.ea);
        delete callinfo;
        return MERR_BADARCH;
      }
      mop_t pointer_mop;
      pointer_mop.make_gvar(target_ea);
      pointer_mop.size = 4;
      arg.copy_mop(pointer_mop);
    }
    else
    {
      arg.copy_mop(args[i].op);
    }
    arg.type = callee_info[i].type;
    arg.name = callee_info[i].name;
    arg.argloc = callee_info[i].argloc;
    arg.ea = cdg.insn.ea;
    // C string args: a constant at a proven
    // pointer-typed position renders as a string literal / &item at
    // CMAT_FINAL. Plain direct/import calls only — the Go-shaped sites
    // render through their own layer, synthetic args are not constants.
    if ( typed_helper == nullptr && !tail_call && !unwind_call && !reference_call
         && !checked_dynamic_indirect && !go_pcf_indirect
         && !(i < synthetic_args.size() && !synthetic_args[i].empty()) )
    {
      const bool go_shaped = go_call_shapes.find(cdg.insn.ea) != go_call_shapes.end()
        || go_call_args_by_call.find(cdg.insn.ea) != go_call_args_by_call.end();
      try_record_c_pointer_argument(state_entry, cdg, imported, target, function_index, helper_name,
                                    callee_param_types, i, args[i], go_shaped);
    }
    if ( options::debug("mc") && go_frame_mode )
    {
      char buf[176];
      int n = qsnprintf(
        buf, sizeof(buf), "M1D2_ARG ea=%a i=%d t=%d size=%d type_sz=%d loc=%d stk=%d\n",
        cdg.insn.ea, int(i), int(arg.t), int(arg.size), int(arg.type.get_size()),
        int(arg.argloc.atype()), arg.argloc.is_stkoff() ? int(arg.argloc.stkoff()) : -1);
      if ( n > 0 )
        ::write(2, buf, size_t(n));
    }
  }
  const int result_size = int(wasm_results_size(callee_result_types));
  const size_t c_frame_bytes = has_c_frame ? size_t(c_frame_metadata.c_frame_bytes) : 0;
  const sval_t result_offset
    = sval_t(wasmhex_overflow_locals_frame_bytes(locals.size(), c_frame_bytes));
  if ( tuple_sret )
  {
    mop_t result_pointer;
    if ( tail_call )
    {
      if ( !has_sret_pointer )
      {
        delete callinfo;
        return MERR_BADARCH;
      }
      result_pointer.make_reg(sret_pointer_reg, 4);
    }
    else
    {
      if ( result_offset < 0 || result_size <= 0 || result_offset > cdg.mba->frsize - result_size )
      {
        delete callinfo;
        return MERR_BADARCH;
      }
      mop_t result_storage;
      result_storage.make_stkvar(cdg.mba, result_offset);
      result_storage.size = NOSIZE;
      mop_addr_t *address = new mop_addr_t(result_storage, NOSIZE, result_size);
      if ( address == nullptr )
      {
        delete callinfo;
        return MERR_MEM;
      }
      result_pointer.t = mop_a;
      result_pointer.a = address;
      result_pointer.size = 4;
    }
    mcallarg_t &arg = callinfo->args[callee_param_types.size()];
    arg.copy_mop(result_pointer);
    arg.type = callee_info[callee_param_types.size()].type;
    arg.name = callee_info[callee_param_types.size()].name;
    arg.argloc = callee_info[callee_param_types.size()].argloc;
    arg.ea = cdg.insn.ea;
  }
  callinfo->return_type = callee_info.rettype;
  callinfo->return_argloc = callee_info.retloc;
  cdg.mba->get_func_output_lists(&callinfo->return_regs, &callinfo->spoiled, callee_type,
                                 cdg.insn.ea, tail_call);
  const ivl_t all_memory = ivl_t::allmem();
  callinfo->visible_memory.clear();
  callinfo->spoiled.mem.clear();
  if ( imported )
  {
    callinfo->flags |= FCI_FINAL;
  }
  if ( no_return )
    callinfo->flags |= FCI_NORET;
  if ( callee_result_types.size() > 1 && !tuple_sret )
  {
    if ( !set_multi_return_regs(callinfo, callee_result_types) )
    {
      delete callinfo;
      return MERR_BADARCH;
    }
  }
  else if ( !tuple_sret && !callee_result_types.empty() )
  {
    mop_t retreg;
    if ( !make_regloc_mop(cdg, callee_info.retloc, int(callee_info.rettype.get_size()), &retreg) )
    {
      delete callinfo;
      return MERR_BADARCH;
    }
    callinfo->retregs.push_back(retreg);
  }
  // tuple_sret passes &result_storage (mop_a in MMIDX_LVARS) to the
  // callee, and wasm_tuple_call writes result_buffer in MMIDX_LVARS. Keep
  // allmem here so Hex-Rays's internal MBA_CALLS / minstkref alias analysis
  // is not bypassed by FCI_SPLOK; kreg locals (< 3072) still propagate freely.
  const bool uses_in_frame_result_buffer = tuple_sret
    || (callee_result_types.size() > 1 && !tuple_sret && int(callee_info.rettype.get_size()) > 8);
  if ( typed_helper != nullptr )
  {
    const wasm_helper_effect_t effect = classify_typed_helper(typed_helper);
    if ( effect == wasm_helper_effect_t::pure_noside )
    {
      callinfo->flags |= FCI_PURE | FCI_NOSIDE | FCI_SPLOK;
      callinfo->spoiled = callinfo->return_regs;
    }
    else if ( effect == wasm_helper_effect_t::trapping_nomem )
    {
      callinfo->flags |= FCI_SPLOK;
      callinfo->spoiled = callinfo->return_regs;
    }
    else if ( effect == wasm_helper_effect_t::memory_or_global )
    {
      if ( no_return || uses_in_frame_result_buffer )
      {
        callinfo->visible_memory.add(all_memory);
        callinfo->spoiled.add(all_memory);
      }
      else
      {
        callinfo->flags |= FCI_SPLOK;
        add_wasm_call_memory_clobber(cdg, &callinfo->visible_memory, &callinfo->spoiled);
      }
    }
    else
    {
      if ( !logged_unknown_helper )
      {
        msg("wasmhex: unclassified helper \"%s\" at %a; falling back to allmem\n", typed_helper,
            cdg.insn.ea);
        logged_unknown_helper = true;
      }
      callinfo->visible_memory.add(all_memory);
      callinfo->spoiled.add(all_memory);
    }
  }
  else if ( no_return || uses_in_frame_result_buffer )
  {
    callinfo->visible_memory.add(all_memory);
    callinfo->spoiled.add(all_memory);
  }
  else
  {
    callinfo->flags |= FCI_SPLOK;
    add_wasm_call_memory_clobber(cdg, &callinfo->visible_memory, &callinfo->spoiled);
  }

  mop_t callee;
  if ( emit_as_icall )
    callee = indirect_callee;
  else if ( imported )
    callee.make_helper(helper_name.c_str());
  else
    callee.make_gvar(target);

  const bool multi_return = callee_result_types.size() > 1;
  const int return_size = tuple_sret ? 0
    : callee_result_types.empty()    ? 0
                                     : int(callee_info.rettype.get_size());
  if ( tail_call )
  {
    mop_t info;
    info._make_callinfo(callinfo);
    info.size = return_size;
    if ( multi_return && !tuple_sret )
      info.set_udt();
    minsn_t *call_insn = cdg.emit(m_call, &callee, nullptr, &info);
    if ( call_insn == nullptr )
      return MERR_INTERR;
    call_insn->set_tailcall();
    return cdg.emit(m_ret, nullptr, nullptr, nullptr) == nullptr ? MERR_INTERR : MERR_OK;
  }

  if ( tuple_sret )
  {
    mop_t info;
    info._make_callinfo(callinfo);
    info.size = 0;
    mop_t call_segment;
    call_segment.make_reg(reg2mreg(wasmhex_ds_reg), 2);
    minsn_t *call_insn = emit_as_icall ? cdg.emit(m_icall, &call_segment, &indirect_callee, &info)
                                       : cdg.emit(m_call, &callee, nullptr, &info);
    if ( call_insn == nullptr )
      return MERR_INTERR;
    sval_t offset = result_offset;
    for ( uchar type : callee_result_types )
    {
      const int size = wasm_type_size(type);
      mop_t destination;
      if ( size == 0 || !make_operand_register(cdg, type, stack.size(), &destination) )
        return MERR_MEM;
      mop_t source;
      source.make_stkvar(cdg.mba, offset);
      source.size = size;
      if ( !emit_call_result_move(cdg, source, destination, type == 0x7D || type == 0x7C) )
        return MERR_INTERR;
      push(destination, type);
      offset += size;
    }
    return MERR_OK;
  }

  if ( multi_return && return_size > 8 )
  {
    if ( result_offset < 0 || return_size <= 0 || result_offset > cdg.mba->frsize - return_size )
    {
      delete callinfo;
      return MERR_BADARCH;
    }
    mop_t result_buffer;
    result_buffer.make_stkvar(cdg.mba, result_offset);
    result_buffer.size = return_size;
    result_buffer.set_udt();
    minsn_t *helper_call = cdg.mba->create_helper_call(
      cdg.insn.ea, "wasm_tuple_call", &callinfo->return_type, &callinfo->args, &result_buffer);
    if ( helper_call == nullptr || helper_call->l.t != mop_d || helper_call->l.d == nullptr
         || helper_call->l.d->d.f == nullptr )
    {
      delete callinfo;
      return MERR_BADARCH;
    }
    minsn_t *inner_call = helper_call->l.d;
    if ( emit_as_icall )
    {
      inner_call->opcode = m_icall;
      inner_call->l.make_reg(reg2mreg(wasmhex_ds_reg), 2);
      inner_call->r = indirect_callee;
    }
    else if ( imported )
      inner_call->l.make_helper(helper_name.c_str());
    else
      inner_call->l.make_gvar(target);
    mcallinfo_t *generated_info = inner_call->d.f;
    generated_info->callee = callinfo->callee;
    generated_info->cc = callinfo->cc;
    generated_info->return_type = callinfo->return_type;
    generated_info->return_argloc = callinfo->return_argloc;
    generated_info->return_regs = callinfo->return_regs;
    generated_info->retregs = callinfo->retregs;
    generated_info->spoiled = callinfo->spoiled;
    generated_info->visible_memory = callinfo->visible_memory;
    generated_info->flags = callinfo->flags;
    inner_call->d.size = return_size;
    inner_call->d.set_udt();
    helper_call->l.size = return_size;
    helper_call->l.set_udt();
    delete callinfo;
    if ( !emit_call_result_move(cdg, helper_call->l, helper_call->d, false) )
      return MERR_INTERR;

    sval_t offset = result_offset;
    for ( uchar type : callee_result_types )
    {
      const int size = wasm_type_size(type);
      mop_t destination;
      if ( size == 0 || !make_operand_register(cdg, type, stack.size(), &destination) )
        return MERR_MEM;
      mop_t source;
      source.make_stkvar(cdg.mba, offset);
      source.size = size;
      if ( !emit_call_result_move(cdg, source, destination, type == 0x7D || type == 0x7C) )
        return MERR_INTERR;
      push(destination, type);
      offset += size;
    }
    return MERR_OK;
  }

  mop_t info;
  info._make_callinfo(callinfo);
  info.size = return_size;
  if ( multi_return )
    info.set_udt();
  mop_t call_segment;
  call_segment.make_reg(reg2mreg(wasmhex_ds_reg), 2);
  minsn_t *call_insn = emit_as_icall ? cdg.emit(m_icall, &call_segment, &indirect_callee, &info)
                                     : cdg.emit(m_call, &callee, nullptr, &info);
  if ( call_insn == nullptr )
    return MERR_INTERR;
  for ( size_t i = 0; i < callee_result_types.size(); ++i )
  {
    const uchar type = callee_result_types[i];
    const int size = wasm_type_size(type);
    mop_t source, destination;
    if ( size == 0 || !make_operand_register(cdg, type, stack.size() + i, &destination) )
      return MERR_BADARCH;
    if ( multi_return )
    {
      size_t offset = 0;
      for ( size_t j = 0; j < i; ++j )
        offset += wasm_type_size(callee_result_types[j]);
      if ( !get_multi_result_component(*callinfo, offset, size, &source) )
        return MERR_BADARCH;
    }
    else if ( callinfo->retregs.size() == 1 )
    {
      source = callinfo->retregs[0];
    }
    else
      return MERR_BADARCH;
    if ( !emit_call_result_move(cdg, source, destination, type == 0x7D || type == 0x7C) )
      return MERR_BADARCH;
    push(destination, type);
  }
  if ( unwind_call )
  {
    if ( imported || !callee_result_types.empty() || !stack_has_prefix(unwind_try_frame->base_stack)
         || stack.size() != unwind_try_frame->base_stack.size() )
      return MERR_BADARCH;
    wasm_branch_resume_t resume;
    resume.target = unwind_catch_ea;
    resume.stack = unwind_try_frame->base_stack;
    if ( has_unwind_payload )
      resume.stack.push_back(unwind_payload);
    auto existing = std::find_if(resume_states.begin(), resume_states.end(),
                                 [unwind_catch_ea](const wasm_branch_resume_t &item)
                                 { return item.target == unwind_catch_ea; });
    if ( existing == resume_states.end() )
      resume_states.push_back(resume);
    stack = unwind_try_frame->base_stack;
    mop_t target_operand;
    target_operand.make_gvar(unwind_catch_ea);
    if ( cdg.emit(m_goto, &target_operand, nullptr, nullptr) == nullptr )
      return MERR_INTERR;
    unreachable = true;
    return MERR_OK;
  }
  return MERR_OK;
}
wasmhex_filter_t::wasm_helper_effect_t
wasmhex_filter_t::classify_typed_helper(const char *helper) const
{
  if ( helper == nullptr || *helper == '\0' )
    return wasm_helper_effect_t::unknown;
  static const char *const pure_helpers[] = {
    "wasm_select_i32", "wasm_select_i64", "wasm_select_f32", "wasm_select_f64", "wasm_select_ref",
    "wasm_i32_clz",    "wasm_i32_ctz",    "wasm_i32_popcnt", "wasm_i64_clz",    "wasm_i64_ctz",
    "wasm_i64_popcnt", "wasm_i32_shl",    "wasm_i32_shr_s",  "wasm_i32_shr_u",  "wasm_i64_shl",
    "wasm_i64_shr_s",  "wasm_i64_shr_u",  "wasm_i32_rotl",   "wasm_i32_rotr",   "wasm_i64_rotl",
    "wasm_i64_rotr",
  };
  for ( const char *candidate : pure_helpers )
    if ( strcmp(helper, candidate) == 0 )
      return wasm_helper_effect_t::pure_noside;
  if ( (starts_with(helper, "wasm_f32_") || starts_with(helper, "wasm_f64_")
        || starts_with(helper, "wasm_i32_trunc_sat_") || starts_with(helper, "wasm_i64_trunc_sat_")
        || starts_with(helper, "wasm_i32_reinterpret_")
        || starts_with(helper, "wasm_i64_reinterpret_"))
       && ends_with(helper, "_bits") )
    return wasm_helper_effect_t::pure_noside;

  static const char *const trapping_nomem_helpers[] = {
    "wasm_i32_div_s",
    "wasm_i32_div_u",
    "wasm_i32_rem_s",
    "wasm_i32_rem_u",
    "wasm_i64_div_s",
    "wasm_i64_div_u",
    "wasm_i64_rem_s",
    "wasm_i64_rem_u",
    "wasm_trap_ref_as_non_null_funcref",
    "wasm_trap_ref_as_non_null_externref",
    "wasm_trap_ref_as_non_null_eqref",
    "wasm_memory_size",
    "__builtin_wasm_memory_size",
    // The compiler trap builtin — noreturn (the caller emits it
    // with no_return, so FCI_NORET is already set) and it touches no
    // memory; classifying it here keeps the call from spoiling allmem
    // (and stops the "unclassified helper" warning on every trap site).
    "__builtin_trap",
  };
  for ( const char *candidate : trapping_nomem_helpers )
    if ( strcmp(helper, candidate) == 0 )
      return wasm_helper_effect_t::trapping_nomem;
  if ( starts_with(helper, "wasm_trap_i32_trunc_") || starts_with(helper, "wasm_trap_i64_trunc_")
       || starts_with(helper, "wasm_memory_out_of_bounds_")
       || starts_with(helper, "wasm_table_lookup") )
    return wasm_helper_effect_t::trapping_nomem;

  if ( starts_with(helper, "wasm_trap_") || starts_with(helper, "wasm_memory_")
       || starts_with(helper, "wasm_data_drop_") || starts_with(helper, "wasm_global_import_")
       || starts_with(helper, "wasm_memory0_") || strcmp(helper, "memmove") == 0
       || strcmp(helper, "memset") == 0 || strcmp(helper, "__builtin_wasm_memory_grow") == 0
       || strcmp(helper, "wasm_atomic_fence_seq_cst") == 0 || strcmp(helper, "wasm_run_start") == 0
       || strcmp(helper, "wasm_call_ctors") == 0 || strcmp(helper, "wasm_apply_data_relocs") == 0
       || strcmp(helper, "wasm_arrow_get_binary_like_casts") == 0 )
    return wasm_helper_effect_t::memory_or_global;

  return wasm_helper_effect_t::unknown;
}
}
