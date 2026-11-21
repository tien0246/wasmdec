#include "ctree.hpp"
#include <chrono>

#include "../../../ida/loader/options.hpp"

#include <unistd.h>

#include <name.hpp>
#include <segment.hpp>
#include <typeinf.hpp>

#include "ctree_types.hpp"

namespace wasmhex
{

// Erase the given statements from any block. A labeled statement
// whose successor already carries a label leaves an empty carrier statement
// bound to the old label - gotos never dangle.
void remove_block_statements(cfunc_t *cfunc, const std::set<cinsn_t *> &targets)
{
  if ( targets.empty() )
    return;
  struct block_cleaner_t : public ctree_visitor_t
  {
    const std::set<cinsn_t *> &targets;
    explicit block_cleaner_t(const std::set<cinsn_t *> &_t) : ctree_visitor_t(CV_FAST), targets(_t)
    {
    }

    int idaapi visit_insn(cinsn_t *insn) override
    {
      if ( insn->op == cit_block && insn->cblock != nullptr )
      {
        cblock_t *b = insn->cblock;
        for ( auto it = b->begin(); it != b->end(); )
        {
          if ( targets.find(&*it) != targets.end() )
          {
            if ( it->label_num >= 0 )
            {
              auto next = it;
              ++next;
              if ( next != b->end() && next->label_num < 0 )
              {
                next->label_num = it->label_num;
              }
              else
              {
                // Keep the label on an empty statement so existing
                // gotos stay bound.
                cinsn_t carrier;
                carrier.op = cit_empty;
                carrier.ea = it->ea;
                carrier.label_num = it->label_num;
                b->insert(it, carrier);
              }
            }
            it = b->erase(it);
          }
          else
          {
            ++it;
          }
        }
      }
      return 0;
    }
  };

  block_cleaner_t cleaner(targets);
  cleaner.apply_to(&cfunc->body, nullptr);
}

namespace
{
// WASMHEX_PHASE_TIMER=1: print one PHASE line per Hex-Rays pass (wall time
// since the previous event, our filter time, final lvar count).
double g_phase_last_ms = 0.0;
ea_t g_phase_entry = BADADDR;
int g_phase_func_lvars = -1;

bool phase_timer_on()
{
  // The diagnostic gates moved behind options::debug (WASMHEX_DEBUG
  // comma list) — the environment is only read in loader/options.cpp.
  return options::debug("phases");
}

double phase_now_ms()
{
  return std::chrono::duration<double, std::milli>(
           std::chrono::steady_clock::now().time_since_epoch())
    .count();
}

double phase_ms_since_last()
{
  const double now_ms = phase_now_ms();
  const double delta = now_ms - g_phase_last_ms;
  g_phase_last_ms = now_ms;
  return delta;
}
} // namespace

// filter.cpp owns the accumulator (RAII around wasmhex_filter_t::apply).
extern double g_filter_apply_ms_total;

ssize_t idaapi wasmhex_hexrays_callback(void *, hexrays_event_t event, va_list va)
{
  if ( event == hxe_func_printed )
  {
    va_list va_printed;
    va_copy(va_printed, va);
    cfunc_t *printed = va_arg(va_printed, cfunc_t *);
    va_end(va_printed);
    if ( printed != nullptr )
    {
      netnode wasm_node(wasmhex_node_name, 0, false);
      wasmhex_bounds_mode_t bounds_mode = wasmhex_bounds_mode_t::hidden;
      if ( get_wasm_bounds_mode(wasm_node, &bounds_mode)
           && bounds_mode == wasmhex_bounds_mode_t::hidden && !options::debug("noboundsnote") )
      {
        // Hidden-bounds mode prepends a bounds=hidden preamble so readers know
        // out-of-bounds accesses read like native memory faults (no checks).
        printed->sv.insert(printed->sv.begin(), simpleline_t("// wasmdec: bounds=hidden"));
      }
    }
  }
  // Phase timing (WASMHEX_PHASE_TIMER=1) — one line per engine pass
  // our filter's own time is read from the accumulator (reset per pass so
  // each PHASE line attributes its share).
  static const bool s_phase_timer = phase_timer_on();
  if ( s_phase_timer )
  {
    va_list va_phase;
    va_copy(va_phase, va);
    if ( event == hxe_microcode || event == hxe_preoptimized || event == hxe_glbopt )
    {
      const mba_t *tmba = va_arg(va_phase, mba_t *);
      if ( tmba != nullptr )
      {
        if ( g_phase_entry != tmba->entry_ea )
        {
          g_phase_entry = tmba->entry_ea;
          g_phase_last_ms = phase_now_ms();
          g_filter_apply_ms_total = 0.0;
          msg("PHASE entry=%a === begin ===\n", tmba->entry_ea);
        }
        else
        {
          const char *name = event == hxe_microcode ? "microcode"
            : event == hxe_preoptimized             ? "preopt"
                                                    : "glbopt";
          msg("PHASE entry=%a %s mat=%d qty=%d +%.1f ms (wasmhex_filter=%.1f ms)\n", tmba->entry_ea,
              name, int(tmba->maturity), tmba->qty, phase_ms_since_last(), g_filter_apply_ms_total);
          g_filter_apply_ms_total = 0.0;
        }
      }
    }
    else if ( event == hxe_maturity )
    {
      cfunc_t *cf = va_arg(va_phase, cfunc_t *);
      const int mat = va_arg(va_phase, int);
      if ( cf != nullptr )
        msg("PHASE entry=%a cmat=%d +%.1f ms\n", cf->entry_ea, mat, phase_ms_since_last());
    }
    else if ( event == hxe_structural )
    {
      cfunc_t *cf = va_arg(va_phase, cfunc_t *);
      if ( cf != nullptr )
        msg("PHASE entry=%a structural +%.1f ms\n", cf->entry_ea, phase_ms_since_last());
    }
    else if ( event == hxe_func_printed )
    {
      cfunc_t *cf = va_arg(va_phase, cfunc_t *);
      if ( cf != nullptr )
      {
        const size_t lvars = cf->get_lvars() != nullptr ? cf->get_lvars()->size() : 0;
        g_phase_func_lvars = int(lvars);
        msg("PHASE entry=%a printed +%.1f ms TOTAL_LVARS=%d\n", cf->entry_ea, phase_ms_since_last(),
            g_phase_func_lvars);
      }
    }
    va_end(va_phase);
  }
  if ( event == hxe_microcode || event == hxe_preoptimized || event == hxe_glbopt )
  {
    mba_t *dump_mba = va_arg(va, mba_t *);
    // WASMHEX_VERIFY=1: run mba_t::verify(true) at engine verify boundaries —
    // idat batch runs skip the GUI's inter-pass microcode verifier.
    if ( dump_mba != nullptr && options::debug("verify") )
    {
      msg("WASMHEX_VERIFY: mba verify mat=%d qty=%d entry=%a\n", int(dump_mba->maturity),
          dump_mba->qty, dump_mba->entry_ea);
      dump_mba->verify(true);
    }
    if ( dump_mba != nullptr && options::debug("mc") )
    {
      char hdr[128];
      int n = qsnprintf(hdr, sizeof(hdr), "MC_DUMP ev=%d mat=%d qty=%d entry=%a\n", int(event),
                        int(dump_mba->maturity), dump_mba->qty, dump_mba->entry_ea);
      ::write(2, hdr, n);
      if ( dump_mba->maturity == MMAT_GENERATED || dump_mba->maturity == MMAT_GLBOPT2 )
      {
        for ( int s = 0; s < dump_mba->qty; ++s )
        {
          mblock_t *b = dump_mba->get_mblock(s);
          char bh[160];
          n = qsnprintf(bh, sizeof(bh), "MC_DUMP B%d [%a..%a] t=%d succ=", s, b->start, b->end,
                        int(b->type));
          ::write(2, bh, n);
          for ( int si = 0; si < b->nsucc(); ++si )
          {
            n = qsnprintf(bh, sizeof(bh), "%d,", b->succ(si));
            ::write(2, bh, n);
          }
          n = qsnprintf(bh, sizeof(bh), " pred=");
          ::write(2, bh, n);
          for ( int pi = 0; pi < b->npred(); ++pi )
          {
            n = qsnprintf(bh, sizeof(bh), "%d,", b->pred(pi));
            ::write(2, bh, n);
          }
          n = qsnprintf(bh, sizeof(bh), "\n");
          ::write(2, bh, n);
          for ( minsn_t *i = b->head; i != nullptr; i = i->next )
          {
            qstring t;
            i->print(&t);
            char opd[96];
            int on = qsnprintf(
              opd, sizeof(opd), " [l.t=%d r.t=%d d.t=%d d.b=%d d.g=%a]", int(i->l.t), int(i->r.t),
              int(i->d.t), i->d.t == mop_b ? int(i->d.b) : -1, i->d.t == mop_v ? i->d.g : ea_t(0));
            t.insert("MC_DUMP   ");
            t.append(opd);
            t.append("\n");
            ::write(2, t.c_str(), t.length());
          }
        }
      }
    }
    return 0;
  }
  if ( event == hxe_maturity )
  {
    cfunc_t *cfunc = va_arg(va, cfunc_t *);
    const int maturity = va_arg(va, int);
    if ( maturity == CMAT_FINAL )
    {
      const bool no_rewriters = options::debug("norewriters");
      if ( !no_rewriters )
      {
        wasmhex_apply_jumpout_rewriter(cfunc);
        wasmhex_apply_idiom_rewriter(cfunc);
        wasmhex_apply_shift_rewriter(cfunc);
        // Fuse single-use copy temporaries (select results etc.).
        wasmhex_apply_copy_fuse_rewriter(cfunc);
        wasmhex_apply_eh_rewriter(cfunc);
        // C++1-C++7: display-level C++ source shape (GOT/invoke
        // demangling, orig$ names, __memory_base string literals,
        // __cxa_throw -> throw). Runs after the EH rewriter: the throw
        // rewrite consumes the invokes it leaves behind.
        if ( !options::debug("nocpp") )
          wasmhex_apply_cpp_rewriter(cfunc);
        // Hide Go stack-growth prologues (module flag 'G').
        wasmhex_apply_go_rewriter(cfunc);
        wasmhex_apply_stack_import_rewriter(cfunc);
        apply_struct_pointer_inference(cfunc);
      }
      clear_wasmhex_filter_for_mba(cfunc->mba);
    }
  }
  return 0;
}

}
