// Fuzz target for the SDK-free front end (parser, typed validator,
// control scan). A crash, sanitizer report or hang is a finding; rejecting
// the input is the normal outcome.
// Two build shapes share this file:
//  - libFuzzer (clang -fsanitize=fuzzer): only LLVMFuzzerTestOneInput is used.
//  - WASMHEX_FUZZ_STANDALONE (any compiler, e.g. GCC + ASan/UBSan): a small
//    deterministic mutation driver in main() replays files and mutates them.
#include "core/validation/validate.hpp"

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t *data, std::size_t size)
{
  if ( size > (1u << 20) )
    return 0;
  wasmhex::loader::module_t module;
  wasmhex::loader::validation_verdict_t verdict{};
  const bool accepted = wasmhex::loader::validate_module(module, data, size, &verdict);
  // Invariant: every rejection carries a structured error code. A rejection
  // without one is a diagnostic-model bug — fail loudly with the input hex.
  if ( !accepted && module.diagnostic.code == wasmhex::loader::error_code::none )
  {
    std::fprintf(stderr, "fuzz_parse: rejection without error code, input hex:\n");
    for ( std::size_t i = 0; i < size; ++i )
      std::fprintf(stderr, "%02X", data[i]);
    std::fprintf(stderr, "\n");
    std::abort();
  }
  return 0;
}

#ifdef WASMHEX_FUZZ_STANDALONE
#include <cstdio>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

namespace
{

struct rng_t
{
  std::uint64_t state;
  std::uint64_t next()
  {
    state ^= state << 13;
    state ^= state >> 7;
    state ^= state << 17;
    return state;
  }
  std::size_t below(std::size_t n) { return n == 0 ? 0 : std::size_t(next() % n); }
};

void mutate(std::vector<std::uint8_t> *bytes, rng_t *rng)
{
  static const std::uint8_t interesting[] = { 0x00, 0x01, 0x40, 0x7F, 0x80, 0xFF, 0x0B, 0x6A };
  const std::size_t edits = 1 + rng->below(4);
  for ( std::size_t i = 0; i < edits && !bytes->empty(); ++i )
  {
    const std::size_t at = 8 + rng->below(bytes->size() > 8 ? bytes->size() - 8 : 1);
    if ( at >= bytes->size() )
      continue;
    switch ( rng->below(4) )
    {
      case 0: (*bytes)[at] = std::uint8_t(rng->next()); break;
      case 1: (*bytes)[at] = interesting[rng->below(sizeof(interesting))]; break;
      case 2: bytes->erase(bytes->begin() + std::ptrdiff_t(at)); break;
      default: bytes->insert(bytes->begin() + std::ptrdiff_t(at), std::uint8_t(rng->next())); break;
    }
  }
}

} // namespace

// Usage: fuzz_parse_standalone <iterations-per-seed> <seed files...>
int main(int argc, char **argv)
{
  if ( argc < 3 )
  {
    std::fprintf(stderr, "usage: %s <iterations-per-seed> <seed.wasm>...\n", argv[0]);
    return 2;
  }
  const unsigned long iterations = std::strtoul(argv[1], nullptr, 10);
  rng_t rng{ 0x9E3779B97F4A7C15ull };
  unsigned long executions = 0;
  for ( int i = 2; i < argc; ++i )
  {
    std::ifstream in(argv[i], std::ios::binary);
    const std::vector<std::uint8_t> seed((std::istreambuf_iterator<char>(in)),
                                         std::istreambuf_iterator<char>());
    LLVMFuzzerTestOneInput(seed.data(), seed.size());
    ++executions;
    for ( unsigned long n = 0; n < iterations; ++n )
    {
      std::vector<std::uint8_t> bytes = seed;
      mutate(&bytes, &rng);
      LLVMFuzzerTestOneInput(bytes.data(), bytes.size());
      ++executions;
    }
  }
  std::printf("fuzz_parse_standalone: %lu executions, no crash\n", executions);
  return 0;
}
#endif
