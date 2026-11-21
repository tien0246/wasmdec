// wasmdec: bounds=hidden
int __usercall main.bump@<r0>(int a1@<r0>)
{
  int v1; // kr04_4
  unsigned int v2; // kr04_4
  __int64 v3; // kr08_8

  v1 = go_sp;
  v2 = v1 - 48;
  go_sp = v2;
  *(_QWORD *)v2 = 851552;
  *(_QWORD *)(v2 + 8) = *(_QWORD *)&byte_2[v2 + 54];
  *(_QWORD *)(v2 + 16) = *(_QWORD *)&byte_2[v2 + 62];
  ((void (__usercall *)(__int64@<r0:r1>))runtime.mapaccess1_faststr)(*(_QWORD *)&byte_2[v2 + 70]);
  *(_DWORD *)&byte_2[v2 + 42] = *(_DWORD *)(unsigned int)*(_QWORD *)&byte_2[v2 + 30];
  *(_QWORD *)v2 = 851552;
  *(_QWORD *)(v2 + 8) = *(_QWORD *)&byte_2[v2 + 54];
  *(_QWORD *)(v2 + 16) = *(_QWORD *)&byte_2[v2 + 62];
  ((void (__usercall *)(__int64@<r0:r1>))runtime.mapassign_faststr)(*(_QWORD *)&byte_2[v2 + 70]);
  v3 = *(_QWORD *)&byte_2[v2 + 30];
  if ( v3 == 0 )
    ((void (__usercall *)())runtime.sigpanic)();
  *(_DWORD *)(unsigned int)v3 = *(_DWORD *)&byte_2[v2 + 42] + 1;
  *(_QWORD *)v2 = 851552;
  *(_QWORD *)(v2 + 8) = *(_QWORD *)&byte_2[v2 + 54];
  *(_QWORD *)(v2 + 16) = *(_QWORD *)&byte_2[v2 + 62];
  ((void (__usercall *)(__int64@<r0:r1>))runtime.mapaccess1_faststr)(*(_QWORD *)&byte_2[v2 + 70]);
  *(_DWORD *)(v2 + 80) = *(_DWORD *)(unsigned int)*(_QWORD *)&byte_2[v2 + 30];
  go_sp = v2 + 56;
  return 0;
}
