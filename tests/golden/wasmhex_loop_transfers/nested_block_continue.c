// wasmdec: bounds=hidden
int __usercall nested_block_continue@<r0>(int a1@<r0>)
{
  unsigned int v1; // kr04_4
  int v2; // kr08_4

  v1 = 0;
  v2 = 0;
  do
  {
    while ( ++v1 == 3 )
      v2 = 7;
    v2 += v1;
  }
  while ( v1 < a1 );
  return v2;
}
