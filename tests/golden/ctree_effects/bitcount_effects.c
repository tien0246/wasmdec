// wasmdec: bounds=hidden
int __usercall bitcount_effects@<r0>(int a1@<r0>)
{
  int v1; // kr04_4

  v1 = effect(a1);
  return std::popcount(v1);
}
