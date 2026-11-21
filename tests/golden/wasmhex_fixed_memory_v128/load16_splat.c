// wasmdec: bounds=hidden
int __usercall load16_splat@<r0>(int a1@<r0>)
{
  __int128 v1; // kr40_16
  unsigned __int8 v2; // kr08_1
  unsigned __int8 v3; // kr08_1
  bool v4; // kr04_1
  __int64 v5; // kr08_8

  *(_QWORD *)&v1 = *(unsigned __int16 *)((unsigned int)a1 + 2LL)
                 | ((unsigned __int64)*(unsigned __int16 *)((unsigned int)a1 + 2LL) << 16)
                 | ((*(unsigned __int16 *)((unsigned int)a1 + 2LL)
                   | ((unsigned __int64)*(unsigned __int16 *)((unsigned int)a1 + 2LL) << 16)) << 32);
  *((_QWORD *)&v1 + 1) = v1;
  v2 = 0;
  *MK_FP(1, v2) = v1;
  v3 = 0;
  v4 = *MK_FP(1, v3) == 0x1234123412341234LL;
  v5 = 8;
  return v4 & (*MK_FP(1, v5) == 0x1234123412341234LL);
}
