// Pure standard-C++17 dataflow framework for the wasmhex core.
// Built for decompiler-side consumers (a future dataflow pass) that need
// dominators, dominance frontiers and generic fixpoint solving per function,
// with caching so repeated queries inside one decompilation stay linear-ish
// instead of recomputing (the third-order blowup the Python pipeline hit).
// Header-only, no IDA SDK types — unit-testable with the plain test runner.
#pragma once
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <unordered_map>
#include <vector>

namespace wasmhex::dataflow
{

using serial_t = std::uint32_t;
constexpr serial_t no_serial = serial_t(-1);

// Dominators — Cooper–Harvey–Kennedy "A Simple, Fast Dominance Algorithm":
// reverse-postorder iteration of `idom` until fixpoint.
// edges: successor lists per block (edges[n][k] = successor serial).
// entry : entry block serial.
// Returns idom[] (idom[entry] == entry itself, unreachable blocks get
// no_serial).
inline std::vector<serial_t>
compute_dominators(size_t n, const std::vector<std::vector<serial_t>> &edges, serial_t entry)
{
  std::vector<serial_t> idom(n, no_serial);
  if ( n == 0 || entry >= n )
    return idom;

  // Post-order DFS from the entry over the reachable subgraph.
  std::vector<int> state(n, 0); // 0 unvisited, 1 open, 2 closed
  std::vector<serial_t> postorder;
  std::vector<serial_t> stack{ entry };
  state[entry] = 1;
  while ( !stack.empty() )
  {
    const serial_t block = stack.back();
    bool descended = false;
    if ( block < edges.size() )
    {
      for ( const serial_t succ : edges[block] )
      {
        if ( succ < n && state[succ] == 0 )
        {
          state[succ] = 1;
          stack.push_back(succ);
          descended = true;
          break;
        }
      }
    }
    if ( descended )
      continue;
    stack.pop_back();
    state[block] = 2;
    postorder.push_back(block);
  }

  // Reverse postorder; po_index ranks blocks for the "smaller idom wins"
  // intersection rule.
  std::vector<serial_t> rpo(postorder.rbegin(), postorder.rend());
  std::vector<serial_t> po_index(n, no_serial);
  for ( size_t i = 0; i < rpo.size(); ++i )
    po_index[rpo[i]] = serial_t(i);

  // Predecessors over the reachable subgraph.
  std::vector<std::vector<serial_t>> preds(n);
  for ( size_t block = 0; block < n && block < edges.size(); ++block )
  {
    if ( po_index[block] == no_serial )
      continue;
    for ( const serial_t succ : edges[block] )
      if ( succ < n && po_index[succ] != no_serial )
        preds[succ].push_back(serial_t(block));
  }

  idom[entry] = entry;
  auto intersect = [&](serial_t a, serial_t b)
  {
    while ( a != b )
    {
      while ( po_index[a] > po_index[b] )
        a = idom[a];
      while ( po_index[b] > po_index[a] )
        b = idom[b];
    }
    return a;
  };

  bool changed = true;
  while ( changed )
  {
    changed = false;
    for ( const serial_t block : rpo )
    {
      if ( block == entry )
        continue;
      serial_t candidate = no_serial;
      for ( const serial_t pred : preds[block] )
      {
        if ( idom[pred] == no_serial )
          continue; // not processed yet
        candidate = candidate == no_serial ? pred : intersect(candidate, pred);
      }
      if ( candidate != no_serial && idom[block] != candidate )
      {
        idom[block] = candidate;
        changed = true;
      }
    }
  }
  return idom;
}

// Dominance frontiers given the CFG (generic version of the above).
// edges: successor lists (the same input as compute_dominators).
inline std::vector<std::vector<serial_t>>
compute_dominance_frontiers(size_t n, const std::vector<std::vector<serial_t>> &edges,
                            const std::vector<serial_t> &idom)
{
  std::vector<std::vector<serial_t>> preds(n);
  for ( size_t block = 0; block < n && block < edges.size(); ++block )
  {
    if ( idom[block] == no_serial )
      continue;
    for ( const serial_t succ : edges[block] )
      if ( succ < n && idom[succ] != no_serial )
        preds[succ].push_back(serial_t(block));
  }

  std::vector<std::vector<serial_t>> frontiers(n);
  for ( size_t block = 0; block < n; ++block )
  {
    if ( idom[block] == no_serial )
      continue;
    if ( preds[block].size() >= 2 )
    {
      for ( const serial_t pred : preds[block] )
      {
        serial_t runner = pred;
        while ( runner != no_serial && runner != idom[block] )
        {
          std::vector<serial_t> &list = frontiers[runner];
          if ( std::find(list.begin(), list.end(), serial_t(block)) == list.end() )
          {
            list.push_back(serial_t(block));
            std::sort(list.begin(), list.end());
          }
          runner = runner == idom[runner] ? no_serial : idom[runner];
        }
      }
    }
  }
  return frontiers;
}

// Generic worklist fixpoint solver.
// visit(block) must return true when the block's out-facts changed (the
// solver then re-enqueues its successors). The solver runs until the
// worklist drains; iteration counts per block are tracked for diagnostics.
template <typename F>
struct worklist_solver_t
{
  size_t iterations = 0;

  void run(size_t n, const std::vector<std::vector<serial_t>> &edges, serial_t entry, F &&visit)
  {
    if ( n == 0 || entry >= n )
      return;
    std::vector<bool> queued(n, false);
    std::vector<serial_t> worklist{ entry };
    queued[entry] = true;
    while ( !worklist.empty() )
    {
      const serial_t block = worklist.back();
      worklist.pop_back();
      queued[block] = false;
      ++iterations;
      if ( !visit(block) )
        continue;
      if ( block < edges.size() )
      {
        for ( const serial_t succ : edges[block] )
        {
          if ( succ < n && !queued[succ] )
          {
            queued[succ] = true;
            worklist.push_back(succ);
          }
        }
      }
    }
  }
};

// Per-function memoization cache.
// Keyed by function identity (entry EA or index); cleared explicitly when a
// function's decompilation ends. Values are arbitrary (dominator trees,
// fact maps, ...).
template <typename K, typename V>
struct function_cache_t
{
  std::unordered_map<K, V> entries;

  template <typename Compute>
  const V &get(const K &key, Compute &&compute)
  {
    const auto it = entries.find(key);
    if ( it != entries.end() )
      return it->second;
    return entries.emplace(key, compute()).first->second;
  }

  void clear() { entries.clear(); }

  size_t size() const { return entries.size(); }
};

} // namespace wasmhex::dataflow
