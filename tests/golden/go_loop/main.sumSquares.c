// wasmdec: bounds=hidden
int __usercall main.sumSquares@<r0>(int a1@<r0>)
{
  int v1; // kr04_4
  int v2; // kr10_4
  int v3; // kr18_4

  v1 = go_sp;
  v2 = 1;
  v3 = 0;
  while ( v2 <= (__int64)*(int *)&byte_2[go_sp + 6] )
  {
    v3 += v2 * v2;
    ++v2;
  }
  *(_DWORD *)(unsigned int)(go_sp + 16) = v3;
  go_sp = v1 + 8;
  return 0;
}
