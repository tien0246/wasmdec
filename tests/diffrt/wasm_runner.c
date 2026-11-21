// C4: batch wasmtime runner. Reads inputs list, calls the named export,
// prints "IN <args> OUT <type>:<hex-bits>" per line (or "TRAP <name>").
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>
#include "wasmtime.h"

static void fail(wasmtime_error_t *err, const char *what)
{
  if ( !err )
    return;
  wasm_byte_vec_t msg;
  wasmtime_error_message(err, &msg);
  fprintf(stderr, "%s: %.*s\n", what, (int)msg.size, msg.data);
  exit(1);
}

static void fail_trap(wasm_trap_t *trap, const char *what)
{
  if ( trap )
  {
    wasm_message_t msg;
    wasm_trap_message(trap, &msg);
    fprintf(stderr, "%s (trap): %.*s\n", what, (int)msg.size, msg.data);
  }
  else
  {
    fprintf(stderr, "%s (trap)\n", what);
  }
  exit(1);
}

static uint64_t parse_value(const char *type, const char *text)
{
  if ( !strcmp(type, "f32") || !strcmp(type, "f64") )
    return strtoull(text, NULL, 16);
  return (uint64_t)strtoll(text, NULL, 10);
}

int main(int argc, char **argv)
{
  if ( argc != 4 )
  {
    fprintf(stderr, "usage: %s <file.wasm> <export> <inputs.txt>\n", argv[0]);
    return 1;
  }
  FILE *inputs = fopen(argv[3], "r");
  if ( !inputs )
  {
    perror("inputs");
    return 1;
  }

  wasm_config_t *config = wasm_config_new();
  wasm_engine_t *engine = wasm_engine_new_with_config(config);
  wasmtime_store_t *store = wasmtime_store_new(engine, NULL, NULL);
  wasmtime_context_t *ctx = wasmtime_store_context(store);

  FILE *f = fopen(argv[1], "rb");
  if ( !f )
  {
    perror(argv[1]);
    return 1;
  }
  fseek(f, 0, SEEK_END);
  long size = ftell(f);
  rewind(f);
  unsigned char *bytes = malloc(size);
  if ( fread(bytes, 1, size, f) != (size_t)size )
    return 1;
  fclose(f);

  wasmtime_module_t *module = NULL;
  wasmtime_error_t *err = wasmtime_module_new(engine, bytes, size, &module);
  fail(err, "module_new");

  wasmtime_instance_t instance;
  wasm_trap_t *init_trap = NULL;
  err = wasmtime_instance_new(ctx, module, NULL, 0, &instance, &init_trap);
  if ( init_trap )
    fail_trap(init_trap, "instance_new");
  fail(err, "instance_new");

  wasmtime_extern_t item;
  if ( !wasmtime_instance_export_get(ctx, &instance, argv[2], strlen(argv[2]), &item) )
  {
    fprintf(stderr, "export %s not found\n", argv[2]);
    return 1;
  }
  wasmtime_func_t func = item.of.func;
  const wasm_functype_t *ftype = wasmtime_func_type(ctx, &func);
  const wasm_valtype_vec_t *params = wasm_functype_params(ftype);
  const wasm_valtype_vec_t *results_ty = wasm_functype_results(ftype);

  char line[1024];
  while ( fgets(line, sizeof(line), inputs) )
  {
    if ( line[0] == '#' || line[0] == '\n' )
      continue;
    wasmtime_val_t args[8];
    size_t nargs = 0;
    char *cursor = line;
    while ( *cursor && nargs < params->size )
    {
      char type[8] = { 0 };
      char value[64] = { 0 };
      if ( sscanf(cursor, "%7[^:]:%63s", type, value) == 2 )
      {
        uint64_t raw = parse_value(type, value);
        if ( !strcmp(type, "i32") )
        {
          args[nargs].kind = WASMTIME_I32;
          args[nargs].of.i32 = (int32_t)(uint32_t)raw;
        }
        else if ( !strcmp(type, "i64") )
        {
          args[nargs].kind = WASMTIME_I64;
          args[nargs].of.i64 = (int64_t)raw;
        }
        else if ( !strcmp(type, "f32") )
        {
          args[nargs].kind = WASMTIME_F32;
          uint32_t bits = (uint32_t)raw;
          memcpy(&args[nargs].of.f32, &bits, 4);
        }
        else if ( !strcmp(type, "f64") )
        {
          args[nargs].kind = WASMTIME_F64;
          memcpy(&args[nargs].of.f64, &raw, 8);
        }
        nargs++;
      }
      cursor = strchr(cursor, ' ');
      if ( !cursor )
        break;
      cursor++;
    }
    if ( nargs < params->size )
      continue;

    printf("IN");
    for ( size_t i = 0; i < nargs; i++ )
    {
      if ( args[i].kind == WASMTIME_I32 )
        printf(" i32:%d", args[i].of.i32);
      else if ( args[i].kind == WASMTIME_I64 )
        printf(" i64:%" PRId64, args[i].of.i64);
      else if ( args[i].kind == WASMTIME_F32 )
      {
        uint32_t b;
        memcpy(&b, &args[i].of.f32, 4);
        printf(" f32:%08x", b);
      }
      else if ( args[i].kind == WASMTIME_F64 )
      {
        uint64_t b;
        memcpy(&b, &args[i].of.f64, 8);
        printf(" f64:%016" PRIx64, b);
      }
    }

    wasmtime_val_t results[2] = { { 0 } };
    wasm_trap_t *trap = NULL;
    err = wasmtime_func_call(ctx, &func, args, nargs, results, results_ty->size, &trap);
    if ( trap )
    {
      printf(" TRAP\n");
      fflush(stdout);
      continue;
    }
    if ( err )
    {
      printf(" ERROR\n");
      fflush(stdout);
      continue;
    }
    for ( size_t i = 0; i < results_ty->size && i < 2; i++ )
    {
      if ( results[i].kind == WASMTIME_I32 )
      {
        printf(" OUT i32:%08x", (uint32_t)results[i].of.i32);
      }
      else if ( results[i].kind == WASMTIME_I64 )
      {
        printf(" OUT i64:%016" PRIx64, (uint64_t)results[i].of.i64);
      }
      else if ( results[i].kind == WASMTIME_F32 )
      {
        uint32_t b;
        memcpy(&b, &results[i].of.f32, 4);
        printf(" OUT f32:%08x", b);
      }
      else if ( results[i].kind == WASMTIME_F64 )
      {
        uint64_t b;
        memcpy(&b, &results[i].of.f64, 8);
        printf(" OUT f64:%016" PRIx64, b);
      }
    }
    printf("\n");
    fflush(stdout);
  }
  fclose(inputs);
  return 0;
}
