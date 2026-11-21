// wasmdec: bounds=hidden
int __usercall main.adder@<r0>(int a1@<r0>)
{
  int v1; // kr04_4
  unsigned int v2; // kr04_4
  __int64 v3; // kr08_8

  v1 = go_sp;
  v2 = v1 - 32;
  go_sp = v2;
  *(_QWORD *)v2 = 16;
  *(_QWORD *)(v2 + 8) = 874936;
  *(_BYTE *)(v2 + 16) = 1;
  ((void (__usercall *)())runtime.mallocgcSmallNoScanSC2)();
  v3 = *(_QWORD *)&byte_2[v2 + 22];
  *(_QWORD *)(unsigned int)v3 = 350355456;
  *(_DWORD *)&byte_2[(unsigned int)v3 + 6] = *(_DWORD *)&byte_2[v2 + 38];
  *(_QWORD *)(v2 + 48) = v3;
  go_sp = v2 + 40;
  return 0;
}
