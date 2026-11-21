// wasmdec: bounds=hidden
int __usercall main.sumSlice@<r0>(int a1@<r0>)
{
  int v1; // kr04_4
  __int64 v2; // kr18_8
  int v3; // kr20_4

  v1 = go_sp;
  v2 = 0;
  v3 = 0;
  while ( v2 < *(_QWORD *)&byte_2[go_sp + 14] )
    v3 += *(_DWORD *)((unsigned int)*(_QWORD *)&byte_2[go_sp + 6] + 4 * (_DWORD)v2++);
  *(_DWORD *)(unsigned int)(go_sp + 32) = v3;
  go_sp = v1 + 8;
  return 0;
}
