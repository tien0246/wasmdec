// Usage: gen_inputs <n> <type1> [type2 ...] > inputs.txt
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdint>
#include <random>
#include <string>
#include <vector>

static uint32_t f32_bits(float f)
{
  uint32_t b;
  memcpy(&b, &f, 4);
  return b;
}

static uint64_t f64_bits(double d)
{
  uint64_t b;
  memcpy(&b, &d, 8);
  return b;
}

int main(int argc, char **argv)
{
  if ( argc < 3 )
  {
    fprintf(stderr, "usage: %s <n> <type1> [type2 ...]\n", argv[0]);
    return 1;
  }
  int n = atoi(argv[1]);
  std::mt19937_64 rng(20261031); // deterministic seed

  std::vector<std::string> types;
  for ( int i = 2; i < argc; i++ )
    types.push_back(argv[i]);

  for ( int row = 0; row < n; row++ )
  {
    for ( size_t col = 0; col < types.size(); col++ )
    {
      if ( col > 0 )
        printf(" ");
      const std::string &t = types[col];
      if ( t == "i32" )
      {
        static const int32_t edges[]
          = { 0, 1, -1, 0x7FFFFFFF, (int32_t)0x80000000, 0x7F, -0x80, 0xFF, 0xFFFF, 100, 1000 };
        static int edge_idx = 0;
        int32_t v;
        if ( edge_idx < 11 && row < 11 )
          v = edges[edge_idx++];
        else
          v = (int32_t)rng();
        printf("i32:%d", v);
      }
      else if ( t == "i64" )
      {
        static const int64_t edges[]
          = { 0, 1, -1, 0x7FFFFFFFFFFFFFFF, (int64_t)0x8000000000000000, 1000000 };
        static int edge_idx = 0;
        int64_t v;
        if ( edge_idx < 6 && row < 6 )
          v = edges[edge_idx++];
        else
          v = (int64_t)rng();
        printf("i64:%lld", (long long)v);
      }
      else if ( t == "f32" )
      {
        static const float edges[] = { 0.0f,
                                       -0.0f,
                                       1.0f,
                                       -1.0f,
                                       0.5f,
                                       -0.5f,
                                       1.5f,
                                       -1.5f,
                                       2.5f,
                                       -2.5f,
                                       __builtin_inff(),
                                       -__builtin_inff(),
                                       __builtin_nanf(""),
                                       3.4028235e38f };
        static int edge_idx = 0;
        float f;
        if ( edge_idx < 14 && row < 14 )
          f = edges[edge_idx++];
        else
        {
          uint32_t bits = (uint32_t)rng();
          memcpy(&f, &bits, 4);
        }
        printf("f32:%08x", f32_bits(f));
      }
      else if ( t == "f64" )
      {
        static const double edges[] = { 0.0,
                                        -0.0,
                                        1.0,
                                        -1.0,
                                        0.5,
                                        -0.5,
                                        1.5,
                                        -1.5,
                                        2.5,
                                        -2.5,
                                        __builtin_inf(),
                                        -__builtin_inf(),
                                        __builtin_nan(""),
                                        100.0 };
        static int edge_idx = 0;
        double d;
        if ( edge_idx < 14 && row < 14 )
          d = edges[edge_idx++];
        else
        {
          uint64_t bits = rng();
          memcpy(&d, &bits, 8);
        }
        printf("f64:%016llx", (unsigned long long)f64_bits(d));
      }
    }
    printf("\n");
  }
  return 0;
}
