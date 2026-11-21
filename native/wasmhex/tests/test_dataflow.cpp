// Dataflow framework unit tests — dominators (CHK), dominance
// frontiers and the worklist solver on three canonical graph shapes.
#include "doctest.h"

#include <functional>

#include "../core/analysis/ssa.hpp"

using namespace wasmhex::dataflow;

namespace
{

std::vector<std::vector<serial_t>> make_edges(const std::vector<std::vector<serial_t>> &list)
{
  return list;
}

} // namespace

TEST_CASE("dataflow: dominators on a linear chain 0->1->2->3")
{
  const auto edges = make_edges({ { 1 }, { 2 }, { 3 }, {} });
  const auto idom = compute_dominators(4, edges, 0);
  REQUIRE(idom[0] == 0);
  CHECK(idom[1] == 0);
  CHECK(idom[2] == 1);
  CHECK(idom[3] == 2);
}

TEST_CASE("dataflow: dominators on a diamond 0->{1,2}->3")
{
  const auto edges = make_edges({ { 1, 2 }, { 3 }, { 3 }, {} });
  const auto idom = compute_dominators(4, edges, 0);
  REQUIRE(idom[0] == 0);
  CHECK(idom[1] == 0);
  CHECK(idom[2] == 0);
  CHECK(idom[3] == 0); // join: both paths reach 3, idom is the entry
}

TEST_CASE("dataflow: dominators on a loop 0->1->2->1, 2->3")
{
  const auto edges = make_edges({ { 1 }, { 2 }, { 1, 3 }, {} });
  const auto idom = compute_dominators(4, edges, 0);
  REQUIRE(idom[0] == 0);
  CHECK(idom[1] == 0); // 1 is on every path from 0 into the loop
  CHECK(idom[2] == 1);
  CHECK(idom[3] == 2);
}

TEST_CASE("dataflow: unreachable blocks get no_serial")
{
  const auto edges = make_edges({ { 1 }, {}, { 3 }, {} }); // 2,3 unreachable
  const auto idom = compute_dominators(4, edges, 0);
  CHECK(idom[0] == 0);
  CHECK(idom[1] == 0);
  CHECK(idom[2] == no_serial);
  CHECK(idom[3] == no_serial);
}

TEST_CASE("dataflow: dominance frontiers")
{
  SUBCASE("diamond: join block is the frontier of both arms")
  {
    const auto edges = make_edges({ { 1, 2 }, { 3 }, { 3 }, {} });
    const auto idom = compute_dominators(4, edges, 0);
    const auto frontiers = compute_dominance_frontiers(4, edges, idom);
    // DF(1) = {3}, DF(2) = {3}, DF(0) = {}, DF(3) = {}
    CHECK(frontiers[0].empty());
    CHECK(frontiers[1].size() == 1);
    CHECK(frontiers[1][0] == 3);
    CHECK(frontiers[2].size() == 1);
    CHECK(frontiers[2][0] == 3);
    CHECK(frontiers[3].empty());
  }
  SUBCASE("loop: the header is in the frontier of the backedge source")
  {
    const auto edges = make_edges({ { 1 }, { 2 }, { 1, 3 }, {} });
    const auto idom = compute_dominators(4, edges, 0);
    const auto frontiers = compute_dominance_frontiers(4, edges, idom);
    // join at 1 (preds 0 and 2): runner from 2 stops at idom(1)=0 -> DF(2)={1}
    CHECK(frontiers[2].size() == 1);
    CHECK(frontiers[2][0] == 1);
  }
  SUBCASE("linear chain has empty frontiers everywhere")
  {
    const auto edges = make_edges({ { 1 }, { 2 }, { 3 }, {} });
    const auto idom = compute_dominators(4, edges, 0);
    const auto frontiers = compute_dominance_frontiers(4, edges, idom);
    for ( const auto &frontier : frontiers )
      CHECK(frontier.empty());
  }
}

TEST_CASE("dataflow: worklist solver reaches fixpoint")
{
  const auto edges = make_edges({ { 1, 2 }, { 3 }, { 3 }, {} });
  std::vector<int> distance(4, -1);
  distance[0] = 0;
  worklist_solver_t<std::function<bool(serial_t)>> solver;
  solver.run(4, edges, 0,
             [&](serial_t block)
             {
               int next = distance[block] + 1;
               bool changed = false;
               for ( const serial_t succ : edges[block] )
               {
                 if ( distance[succ] < next )
                 {
                   distance[succ] = next;
                   changed = true;
                 }
               }
               return changed;
             });
  CHECK(distance[0] == 0);
  CHECK(distance[1] == 1);
  CHECK(distance[2] == 1);
  CHECK(distance[3] == 2);
  CHECK(solver.iterations <= 8); // small graph, bounded work
}

TEST_CASE("dataflow: function cache memoizes")
{
  function_cache_t<int, int> cache;
  int computes = 0;
  const int &a = cache.get(1,
                           [&]()
                           {
                             ++computes;
                             return 42;
                           });
  const int &b = cache.get(1,
                           [&]()
                           {
                             ++computes;
                             return 99;
                           });
  CHECK(a == 42);
  CHECK(b == 42);
  CHECK(computes == 1);
  cache.clear();
  const int &c = cache.get(1,
                           [&]()
                           {
                             ++computes;
                             return 7;
                           });
  CHECK(c == 7);
  CHECK(computes == 2);
  CHECK(cache.size() == 1);
}
