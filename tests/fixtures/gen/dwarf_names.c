/* P5-1 DWARF names fixture (compiled with clang --target=wasm32 -g).
 *
 * Exercises the three DWARF name sources the loader supports:
 *  - an exported function with parameters and named locals (compute_distance),
 *  - a file-static function that is never exported (scale_static),
 *  - a file-static global (origin) landing in a data segment.
 */
struct Point { int x; int y; };
static struct Point origin = { 3, 4 };

static int scale_static(int factor, int base) {
  int product = factor * base;
  return product + origin.x;
}

int compute_distance(struct Point *a, struct Point *b) {
  int dx = a->x - b->x;
  int dy = a->y - b->y;
  return dx * dx + dy * dy + scale_static(dx, dy);
}
