/* P7-3 differential fixture (compiled with clang --target=wasm32 at -O0 and
 * -O2). Ten self-contained functions whose structural shape survives across
 * optimization levels differently, letting the checker pin which forms the
 * decompiler renders at each level.
 */

struct Rec {
  int a;      /* field_0  */
  int b;      /* field_4  */
  double c;   /* field_8  */
  char d[8];  /* field_16 */
  int e;      /* field_24 */
};

int struct_fields(struct Rec *r) {
  return r->a + r->e + (int)r->c;
}

int array_sum(const int *values, int count) {
  int total = 0;
  for (int i = 0; i < count; ++i)
    total += values[i];
  return total;
}

int nested_if(int a, int b, int c) {
  if (a > 0) {
    if (b > 0) {
      if (c > 0)
        return 1;
      return 2;
    }
    return 3;
  }
  return 4;
}

int switch_dispatch(int op, int value) {
  switch (op) {
  case 1:
    return value + 10;
  case 2:
    return value + 20;
  case 3:
    return value + 30;
  default:
    return -1;
  }
}

static int helper(int x) { return 3 * x; }

int call_two_levels(int x) { return helper(x) * 3 + 4; }

int pointer_arith(const int *base, int index) {
  const int *p = base + index;
  return p[0] + p[1];
}

double float_ops(double x) {
  double clamped = x < 0.0 ? 0.0 : (x > 100.0 ? 100.0 : x);
  return clamped * 2.0;
}

int factorial(int n) {
  if (n <= 1)
    return 1;
  return n * factorial(n - 1);
}

int string_len(const char *s) {
  int n = 0;
  while (s[n] != 0)
    ++n;
  return n;
}

int loop_bound(int n) {
  int total = 0;
  for (int i = 0; i < n; ++i)
    total += i * 2;
  return total;
}
