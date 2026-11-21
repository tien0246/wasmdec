// Public-release corpus gate for the core unit tests.
//
// The large sweep corpora live under the git-ignored wasm-test/ tree, so a
// public clone runs core_tests without them. Test cases that read corpus
// files directly open with WASMHEX_REQUIRE_CORPUS_FILE(<repo-relative path>):
//   - default build (WASMHEX_REQUIRE_CORPUS=OFF, the public default): an
//     absent corpus prints ONE notice line — where to fetch it — and the
//     test case returns green (skip-with-notice, matching the corpus-check
//     ctest behavior).
//   - WASMHEX_REQUIRE_CORPUS=ON (sweep machines / release CI): the same
//     absence FAILS the case — strict mode.
// WASMHEX_REPO_DIR is the repo root baked in by the tests CMakeLists.
#pragma once

#include "doctest.h"

#include <cstdio>
#include <string>
#include <sys/stat.h>

namespace wasmhex::test
{
inline bool corpus_present(const std::string &repo_rel_path)
{
  const std::string full = std::string(WASMHEX_REPO_DIR) + repo_rel_path;
  struct stat st = {};
  if ( ::stat(full.c_str(), &st) == 0 && (st.st_mode & S_IFMT) == S_IFREG )
    return true;
#if defined(WASMHEX_REQUIRE_CORPUS_TESTS)
  std::fprintf(stderr,
               "CORPUS-FAIL: %s absent but WASMHEX_REQUIRE_CORPUS=ON — "
               "fetch/rebuild per tests/CORPUS.md\n",
               repo_rel_path.c_str());
#else
  std::printf("CORPUS-SKIP: %s absent — wasm-test/ is git-ignored; fetch/rebuild per "
              "tests/CORPUS.md (configure -DWASMHEX_REQUIRE_CORPUS=ON to enforce)\n",
              repo_rel_path.c_str());
#endif
  return false;
}
} // namespace wasmhex::test

#if defined(WASMHEX_REQUIRE_CORPUS_TESTS)
#define WASMHEX_REQUIRE_CORPUS_FILE(rel)                                                           \
  REQUIRE_MESSAGE(::wasmhex::test::corpus_present(rel), "corpus required but absent: " rel)
#else
#define WASMHEX_REQUIRE_CORPUS_FILE(rel)                                                           \
  do                                                                                               \
  {                                                                                                \
    if ( !::wasmhex::test::corpus_present(rel) )                                                   \
      return;                                                                                      \
  } while ( 0 )
#endif
