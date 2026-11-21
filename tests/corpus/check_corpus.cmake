# V2 (todo §10) / public-release: corpus manifest check — every entry in
# manifest.tsv must be present with the pinned size and SHA-256.
#
# Two modes (root CMakeLists passes WASMHEX_REQUIRE_CORPUS through):
#   - default (OFF): the wasm-test/ corpora are git-ignored, so a public
#     clone has none — every MISSING entry then reports one notice line
#     telling how to fetch the corpora (tests/CORPUS.md) and the check
#     PASSES (skip-with-notice). A PRESENT file with the wrong size/SHA
#     still FAILS (corruption is never skipped), and so does an empty
#     manifest (no vacuous pass).
#   - WASMHEX_REQUIRE_CORPUS=ON (sweep machines / release CI): a missing or
#     corrupted corpus is a FAILURE, reported with the per-corpus
#     recovery hint (source column / tests/CORPUS.md).
#
# Run standalone:  cmake -D WASMHEX_CORPUS_MANIFEST=<repo>/tests/corpus/manifest.tsv \
#                      [-D WASMHEX_REQUIRE_CORPUS=ON] \
#                      -P <repo>/tests/corpus/check_corpus.cmake
# Registered as the `corpus-check` ctest entry by the root CMakeLists (works
# in SDK-free configures — plain CMake, no IDA, no Python).

if(NOT DEFINED WASMHEX_CORPUS_MANIFEST OR "${WASMHEX_CORPUS_MANIFEST}" STREQUAL "")
  # Default: manifest lives next to this script; repo root is two levels up.
  get_filename_component(_corpus_dir "${CMAKE_CURRENT_LIST_DIR}" ABSOLUTE)
  set(WASMHEX_CORPUS_MANIFEST "${_corpus_dir}/manifest.tsv")
else()
  get_filename_component(_corpus_manifest_abs "${WASMHEX_CORPUS_MANIFEST}" ABSOLUTE)
  set(WASMHEX_CORPUS_MANIFEST "${_corpus_manifest_abs}")
endif()
get_filename_component(_repo_root "${WASMHEX_CORPUS_MANIFEST}" DIRECTORY)
get_filename_component(_repo_root "${_repo_root}/../.." ABSOLUTE)

if(NOT EXISTS "${WASMHEX_CORPUS_MANIFEST}")
  message(FATAL_ERROR "V2 corpus check: manifest not found at ${WASMHEX_CORPUS_MANIFEST}")
endif()

file(STRINGS "${WASMHEX_CORPUS_MANIFEST}" _corpus_lines ENCODING UTF-8)
set(_corpus_count 0)
set(_corpus_missing 0)
set(_corpus_corrupt 0)
foreach(_line IN LISTS _corpus_lines)
  string(STRIP "${_line}" _line)
  if(_line STREQUAL "" OR _line MATCHES "^#")
    continue()
  endif()
  # Protect embedded semicolons (CMake list separators) before splitting on
  # tabs, then restore them per field.
  string(REPLACE ";" "|WASMHEX_SC|" _line_esc "${_line}")
  string(REPLACE "\t" ";" _fields "${_line_esc}")
  list(LENGTH _fields _field_count)
  if(NOT _field_count EQUAL 7)
    math(EXPR _corpus_corrupt "${_corpus_corrupt} + 1")
    message("corpus-check: MALFORMED line (${_field_count} fields, expected 7): ${_line}")
    continue()
  endif()
  list(GET _fields 0 _name)
  list(GET _fields 1 _path)
  list(GET _fields 2 _sha)
  list(GET _fields 3 _bytes)
  list(GET _fields 4 _toolchain)
  list(GET _fields 5 _role)
  list(GET _fields 6 _source)
  foreach(_f _name _path _sha _bytes _toolchain _role _source)
    string(REPLACE "|WASMHEX_SC|" ";" ${_f} "${${_f}}")
  endforeach()
  math(EXPR _corpus_count "${_corpus_count} + 1")

  if(NOT EXISTS "${_repo_root}/${_path}")
    math(EXPR _corpus_missing "${_corpus_missing} + 1")
    if(WASMHEX_REQUIRE_CORPUS)
      message("corpus-check: MISSING ${_name} — ${_path} not found.")
      message("corpus-check:   role: ${_role}; source/rebuild: ${_source} (tests/CORPUS.md)")
    endif()
    continue()
  endif()
  file(SIZE "${_repo_root}/${_path}" _actual_bytes)
  if(NOT _actual_bytes EQUAL _bytes)
    math(EXPR _corpus_corrupt "${_corpus_corrupt} + 1")
    message("corpus-check: SIZE MISMATCH ${_name} — expected ${_bytes} bytes, found ${_actual_bytes}.")
    continue()
  endif()
  file(SHA256 "${_repo_root}/${_path}" _actual_sha)
  if(NOT _actual_sha STREQUAL _sha)
    math(EXPR _corpus_corrupt "${_corpus_corrupt} + 1")
    message("corpus-check: SHA MISMATCH ${_name} — expected ${_sha}, found ${_actual_sha}.")
    continue()
  endif()
  message("corpus-check: OK ${_name} (${_bytes} bytes, ${_toolchain})")
endforeach()

if(_corpus_count EQUAL 0)
  message(FATAL_ERROR "V2 corpus check: manifest has no entries — refusing a vacuous pass.")
endif()

if(_corpus_missing GREATER 0 AND NOT WASMHEX_REQUIRE_CORPUS)
  # Skip-with-notice (public default): the corpora are deliberately not in
  # git; outsiders are told how to get them, sweep machines opt into strict.
  message("corpus-check: SKIP ${_corpus_missing}/${_corpus_count} corpora absent — "
          "the wasm-test/ tree is git-ignored; see tests/CORPUS.md for how to fetch/rebuild "
          "each corpus. Configure with -DWASMHEX_REQUIRE_CORPUS=ON to enforce presence.")
  if(_corpus_corrupt GREATER 0)
    message(FATAL_ERROR "V2 corpus check: ${_corpus_corrupt} present corpus entries are "
                        "corrupted (size/SHA mismatch) — corruption is never skipped.")
  endif()
  message("V2 corpus check: skipped (corpora absent, not required).")
endif()

if(_corpus_missing GREATER 0 AND WASMHEX_REQUIRE_CORPUS)
  message(FATAL_ERROR "V2 corpus check: ${_corpus_missing}/${_corpus_count} corpus entries "
                      "missing (WASMHEX_REQUIRE_CORPUS=ON). Restore per tests/CORPUS.md — "
                      "a partial corpus invalidates every pinned sweep hash.")
endif()
if(_corpus_corrupt GREATER 0)
  message(FATAL_ERROR "V2 corpus check: ${_corpus_corrupt}/${_corpus_count} corpus entries "
                      "corrupted (missing or wrong size/SHA). Restore per tests/CORPUS.md — "
                      "a partial corpus invalidates every pinned sweep hash.")
endif()
message("V2 corpus check: all ${_corpus_count} corpora present and hash-verified.")
