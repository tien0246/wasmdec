// C4 stub header: defines every scalar wasm helper that appears in golden
// pseudocode, matching WebAssembly semantics exactly. V1 scope: scalar-only.
#ifndef C4_RUNTIME_STUBS_H
#define C4_RUNTIME_STUBS_H
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
// Use compiler builtins (portable across arm64/x86)
#define __lzcnt(x) ((x) == 0 ? 32 : __builtin_clz(x))
#define __lzcnt64(x) ((x) == 0 ? 64 : __builtin_clzll(x))
#define __tzcnt(x) __builtin_ctz(x)
#define __tzcnt64(x) __builtin_ctzll(x)
#define __popcnt(x) __builtin_popcount(x)
#define __popcnt64(x) __builtin_popcountll(x)
static inline uint32_t __rolr(uint32_t x, int r)
{
  r &= 31;
  return (x << r) | (x >> (32 - r));
}
static inline uint32_t __rorr(uint32_t x, int r)
{
  r &= 31;
  return (x >> r) | (x << (32 - r));
}

// Trap: print and exit with a distinctive code so the harness can match.
static const char *__c4_trap_name = NULL;
static inline void __c4_trap(const char *name)
{
  __c4_trap_name = name;
  fprintf(stderr, "C4_TRAP %s\n", name);
  exit(42);
}

// C shadow-stack pointer (appears in -O0 pseudocode): backed by a static
// buffer so raw-address dereferences don't segfault in V1. Real wasm linear
// memory is not modeled — functions using it are V2 scope.
static unsigned char __c4_linear_memory[16 * 1024 * 1024];
int wasmhex_global_0 = (int)(16 * 1024 * 1024) - 4096; // top of linear memory

// Hex-Rays typedefs.
typedef unsigned int _DWORD;
typedef unsigned long long _QWORD;
typedef unsigned char _BYTE;
typedef unsigned short _WORD;

// --- wasm_i32_* ---
static inline int32_t wasm_i32_div_s(int32_t a, int32_t b)
{
  if ( b == 0 )
    __c4_trap("div0");
  if ( a == INT32_MIN && b == -1 )
    __c4_trap("overflow");
  return a / b;
}
static inline uint32_t wasm_i32_div_u(uint32_t a, uint32_t b)
{
  if ( b == 0 )
    __c4_trap("div0");
  return a / b;
}
static inline int32_t wasm_i32_rem_s(int32_t a, int32_t b)
{
  if ( b == 0 )
    __c4_trap("div0");
  return a % b;
}
static inline uint32_t wasm_i32_rem_u(uint32_t a, uint32_t b)
{
  if ( b == 0 )
    __c4_trap("div0");
  return a % b;
}
static inline int32_t wasm_i32_shl(int32_t a, int32_t b)
{
  return a << (b & 31);
}
static inline int32_t wasm_i32_shr_s(int32_t a, int32_t b)
{
  return a >> (b & 31);
}
static inline uint32_t wasm_i32_shr_u(uint32_t a, uint32_t b)
{
  return a >> (b & 31);
}
static inline int32_t wasm_i32_rotl(int32_t a, int32_t b)
{
  return (int32_t)__rolr((uint32_t)a, (b & 31));
}
static inline int32_t wasm_i32_rotr(int32_t a, int32_t b)
{
  return (int32_t)__rorr((uint32_t)a, (b & 31));
}
static inline int32_t wasm_i32_clz(int32_t a)
{
  return a == 0 ? 32 : __lzcnt((uint32_t)a);
}
static inline int32_t wasm_i32_ctz(int32_t a)
{
  return __tzcnt((uint32_t)a);
}
static inline int32_t wasm_i32_popcnt(int32_t a)
{
  return __popcnt((uint32_t)a);
}
static inline int32_t wasm_i32_eqz(int32_t a)
{
  return a == 0;
}
static inline int32_t wasm_i32_extend_i8_s(int32_t a)
{
  return (int8_t)a;
}
static inline int32_t wasm_i32_extend_i16_s(int32_t a)
{
  return (int16_t)a;
}
static inline uint32_t wasm_i32_extend_i8_u(uint32_t a)
{
  return (uint8_t)a;
}
static inline uint32_t wasm_i32_extend_i16_u(uint32_t a)
{
  return (uint16_t)a;
}
static inline int32_t wasm_i32_trunc_sat_f32_s(float f)
{
  if ( isnan(f) )
    return 0;
  if ( f >= 2147483648.0f )
    return INT32_MAX;
  if ( f <= -2147483649.0f )
    return INT32_MIN;
  return (int32_t)f;
}
static inline int32_t wasm_i32_trunc_sat_f64_s(double d)
{
  if ( isnan(d) )
    return 0;
  if ( d >= 2147483648.0 )
    return INT32_MAX;
  if ( d <= -2147483649.0 )
    return INT32_MIN;
  return (int32_t)d;
}
static inline uint32_t wasm_i32_reinterpret_f32_bits(uint32_t bits)
{
  return bits;
}

// --- wasm_i64_* ---
static inline int64_t wasm_i64_div_s(int64_t a, int64_t b)
{
  if ( b == 0 )
    __c4_trap("div0");
  if ( a == INT64_MIN && b == -1 )
    __c4_trap("overflow");
  return a / b;
}
static inline uint64_t wasm_i64_div_u(uint64_t a, uint64_t b)
{
  if ( b == 0 )
    __c4_trap("div0");
  return a / b;
}
static inline int64_t wasm_i64_rem_s(int64_t a, int64_t b)
{
  if ( b == 0 )
    __c4_trap("div0");
  return a % b;
}
static inline uint64_t wasm_i64_rem_u(uint64_t a, uint64_t b)
{
  if ( b == 0 )
    __c4_trap("div0");
  return a % b;
}
static inline int64_t wasm_i64_shl(int64_t a, int64_t b)
{
  return a << (b & 63);
}
static inline int64_t wasm_i64_shr_s(int64_t a, int64_t b)
{
  return a >> (b & 63);
}
static inline uint64_t wasm_i64_shr_u(uint64_t a, uint64_t b)
{
  return a >> (b & 63);
}
static inline int64_t wasm_i64_clz(int64_t a)
{
  return a == 0 ? 64 : __lzcnt64((uint64_t)a);
}
static inline int64_t wasm_i64_ctz(int64_t a)
{
  return __tzcnt64((uint64_t)a);
}
static inline int64_t wasm_i64_popcnt(int64_t a)
{
  return __popcnt64((uint64_t)a);
}
static inline uint64_t wasm_i64_reinterpret_f64_bits(uint64_t bits)
{
  return bits;
}

// --- wasm_f32_/f64_ (bit-view helpers take raw uint bits) ---
static inline uint32_t wasm_f32_ceil_bits(uint32_t b)
{
  float f;
  memcpy(&f, &b, 4);
  f = ceilf(f);
  memcpy(&b, &f, 4);
  return b;
}
static inline uint32_t wasm_f32_floor_bits(uint32_t b)
{
  float f;
  memcpy(&f, &b, 4);
  f = floorf(f);
  memcpy(&b, &f, 4);
  return b;
}
static inline uint32_t wasm_f32_trunc_bits(uint32_t b)
{
  float f;
  memcpy(&f, &b, 4);
  f = truncf(f);
  memcpy(&b, &f, 4);
  return b;
}
static inline uint32_t wasm_f32_sqrt_bits(uint32_t b)
{
  float f;
  memcpy(&f, &b, 4);
  f = sqrtf(f);
  memcpy(&b, &f, 4);
  return b;
}
static inline uint64_t wasm_f64_ceil_bits(uint64_t b)
{
  double d;
  memcpy(&d, &b, 8);
  d = ceil(d);
  memcpy(&b, &d, 8);
  return b;
}
static inline uint64_t wasm_f64_floor_bits(uint64_t b)
{
  double d;
  memcpy(&d, &b, 8);
  d = floor(d);
  memcpy(&b, &d, 8);
  return b;
}
static inline uint64_t wasm_f64_trunc_bits(uint64_t b)
{
  double d;
  memcpy(&d, &b, 8);
  d = trunc(d);
  memcpy(&b, &d, 8);
  return b;
}
static inline uint64_t wasm_f64_sqrt_bits(uint64_t b)
{
  double d;
  memcpy(&d, &b, 8);
  d = sqrt(d);
  memcpy(&b, &d, 8);
  return b;
}
static inline uint64_t wasm_f64_min_bits(uint64_t a, uint64_t b)
{
  double x, y;
  memcpy(&x, &a, 8);
  memcpy(&y, &b, 8);
  double r = (x < y) ? x : (y < x) ? y : (signbit(x) ? x : y);
  memcpy(&a, &r, 8);
  return a;
}
static inline uint64_t wasm_f64_max_bits(uint64_t a, uint64_t b)
{
  double x, y;
  memcpy(&x, &a, 8);
  memcpy(&y, &b, 8);
  double r = (x > y) ? x : (y > x) ? y : (signbit(x) ? y : x);
  memcpy(&a, &r, 8);
  return a;
}
static inline uint64_t wasm_f64_nearest_bits(uint64_t b)
{
  double d;
  memcpy(&d, &b, 8);
  double r = nearbyint(d); // glibc default = round-half-even, matches wasm
  memcpy(&b, &r, 8);
  return b;
}

// --- select ---
static inline int32_t wasm_select_i32(int32_t a, int32_t b, int32_t c)
{
  return c ? a : b;
}
static inline int64_t wasm_select_i64(int64_t a, int64_t b, int32_t c)
{
  return c ? a : b;
}
static inline uint32_t wasm_select_f32(uint32_t a, uint32_t b, int32_t c)
{
  return c ? a : b;
}
static inline uint64_t wasm_select_f64(uint64_t a, uint64_t b, int32_t c)
{
  return c ? a : b;
}
static inline int32_t wasm_select_ref(int32_t a, int32_t b, int32_t c)
{
  return c ? a : b;
}

// --- C23 float names (match wasm NaN/±0 semantics) ---
static inline double fminimum(double a, double b)
{
  if ( isnan(a) || isnan(b) )
    return NAN;
  if ( a == 0 && b == 0 )
    return signbit(a) ? a : b;
  return a < b ? a : b;
}
static inline double fmaximum(double a, double b)
{
  if ( isnan(a) || isnan(b) )
    return NAN;
  if ( a == 0 && b == 0 )
    return signbit(a) ? b : a;
  return a > b ? a : b;
}
static inline float fminimumf(float a, float b)
{
  if ( isnan(a) || isnan(b) )
    return NAN;
  if ( a == 0 && b == 0 )
    return signbit(a) ? a : b;
  return a < b ? a : b;
}
static inline float fmaximumf(float a, float b)
{
  if ( isnan(a) || isnan(b) )
    return NAN;
  if ( a == 0 && b == 0 )
    return signbit(a) ? b : a;
  return a > b ? a : b;
}

#endif // C4_RUNTIME_STUBS_H
