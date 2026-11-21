// Minimal idalib test — can we open a .wasm and list functions?
// This is the foundation for a pure C++ golden runner (no IDAPython).
#include <idalib.hpp>
#include <cstdio>

int main(int argc, char **argv)
{
  if ( argc < 2 )
  {
    fprintf(stderr, "usage: %s <module.wasm>\n", argv[0]);
    return 1;
  }
  if ( init_library() != 0 )
  {
    fprintf(stderr, "idalib: init failed\n");
    return 1;
  }
  fprintf(stderr, "idalib: init OK\n");

  int rc = open_database(argv[1], true);
  if ( rc != 0 )
  {
    fprintf(stderr, "idalib: open_database rc=%d\n", rc);
    return 1;
  }
  fprintf(stderr, "idalib: open_database OK\n");

  // If we get here without crashing, the foundation works.
  // Full function enumeration + decompile requires the wasmhex processor
  // filter which may need additional setup under idalib — deferred to
  // integration with the main CMake build (target: wasmhex_dump).
  fprintf(stderr, "idalib: FOUNDATION OK (open + auto-analysis survived)\n");
  return 0;
}
