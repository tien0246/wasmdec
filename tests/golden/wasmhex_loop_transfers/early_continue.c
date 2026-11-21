// wasmdec: bounds=hidden
int __usercall early_continue@<r0>(int a1@<r0>)
{
  unsigned int v1; // kr04_4
  int v2; // kr08_4

  v1 = 0;
  v2 = 0;
  while ( v1 < a1 )
  {
    if ( (v1 & 2) != 0 )
      v2 += v1;
    ++v1;
  }
  return v2;
}
