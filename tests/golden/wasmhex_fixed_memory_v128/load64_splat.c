// wasmdec: bounds=hidden
int __usercall load64_splat@<r0>(int a1@<r0>)
{
  __int128 v1; // kr20_16
  unsigned __int8 v2; // kr08_1
  unsigned __int8 v3; // kr08_1
  bool v4; // kr04_1
  __int64 v5; // kr08_8

  *(_QWORD *)&v1 = *(_QWORD *)((unsigned int)a1 + 8LL);
  *((_QWORD *)&v1 + 1) = v1;
  v2 = 0;
  *MK_FP(1, v2) = v1;
  v3 = 0;
  v4 = *MK_FP(1, v3) == 0x123456789ABCDEF0LL;
  v5 = 8;
  return v4 & (*MK_FP(1, v5) == 0x123456789ABCDEF0LL);
}
