#include <idalib.hpp>
#include <hexrays.hpp>
#include <ida.hpp>
#include <funcs.hpp>
#include <name.hpp>
#include <auto.hpp>
#include <diskio.hpp>
#include <lines.hpp>
#include <pro.h>

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

int main(int argc, char **argv)
{
  if ( argc < 2 )
  {
    fprintf(stderr, "usage: %s <module.wasm>\n", argv[0]);
    return 1;
  }

  // Initialize idalib (no argc/argv — headless)
  if ( init_library() != 0 )
  {
    fprintf(stderr, "idalib init failed\n");
    return 1;
  }

  // Open database (create from binary)
  int rc = open_database(argv[1], /*load=*/true);
  if ( rc != 0 )
  {
    fprintf(stderr, "open_database failed: %d\n", rc);
    return 1;
  }

  // Wait for auto-analysis
  auto_wait();

  // Install Hex-Rays + wasmhex filter
  if ( !init_hexrays_plugin() )
  {
    fprintf(stderr, "hexrays init failed\n");
    return 1;
  }

  // The wasmhex processor's F5 filter installs via the processor module.
  // With idalib, we need to load the processor's decompiler hooks.
  // This is the part that used to crash — if it still crashes, the
  // golden_runner (idat + IDAPython) remains the fallback.
  // TODO(Native): verify filter install path under idalib.

  // Decompile every function
  int total = 0, ok = 0, interr = 0;
  for ( ea_t ea = get_first_func(); ea != BADADDR; ea = get_next_func(ea) )
  {
    func_t *fn = get_func(ea);
    if ( !fn )
      continue;
    total++;

    char name[MAXNAMELEN];
    get_name(ea, name, sizeof(name));

    // Decompile
    hexrays_failure_t failure;
    cfunc_t *cfunc = decompile_func(fn, &failure);
    if ( !cfunc )
    {
      printf("ORACLE_FUNC %d %s INTERR 0 0 0 0 code=%d errea=0x%llx\n", total, name, failure.code,
             (unsigned long long)failure.errea);
      interr++;
      continue;
    }

    // Verify
    bool verify_ok = true;
    // cfunc->verify(true, true) — may not be accessible via idalib C API
    // TODO: check if verify is available in idalib mode

    // Get pseudocode text
    std::string text;
    for ( int i = 0; i < cfunc->pseudocode.size(); i++ )
    {
      qstring line = tag_remove(cfunc->pseudocode[i].line);
      text += line.c_str();
      text += "\n";
    }

    int lines = (int)std::count(text.begin(), text.end(), '\n');
    int stack = 0, jumpout = 0;
    // Count STACK[ and JUMPOUT( occurrences
    size_t pos = 0;
    while ( (pos = text.find("STACK[", pos)) != std::string::npos )
    {
      stack++;
      pos++;
    }
    pos = 0;
    while ( (pos = text.find("JUMPOUT(", pos)) != std::string::npos )
    {
      jumpout++;
      pos++;
    }

    printf("ORACLE_FUNC %d %s OK 0 %d %d %d \n", total, name, lines, stack, jumpout);
    printf("ORACLE_BEGIN %s\n%sORACLE_END %s\n", name, text.c_str(), name);
    ok++;
  }

  printf("ORACLE_DONE total=%d ok=%d interr=%d\n", total, ok, interr);

  // Close database
  close_database();
  return 0;
}
