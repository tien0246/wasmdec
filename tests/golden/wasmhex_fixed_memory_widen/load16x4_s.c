// wasmdec: bounds=hidden
int __usercall load16x4_s@<r0>(int a1@<r0>)
{
  __int128 v1; // kr90_16
  __int64 v2; // kr08_8
  __int64 v3; // kr08_8
  bool v4; // kr04_1
  __int64 v5; // kr08_8

  *(_QWORD *)&v1 = (unsigned int)(__int16)*(_QWORD *)((unsigned int)a1 + 1LL)
                 | ((unsigned __int64)(unsigned int)(__int16)WORD1(*(_QWORD *)((unsigned int)a1 + 1LL)) << 32);
  *((_QWORD *)&v1 + 1) = (unsigned int)(__int16)WORD2(*(_QWORD *)((unsigned int)a1 + 1LL))
                       | ((unsigned __int64)(unsigned int)(__int16)HIWORD(*(_QWORD *)((unsigned int)a1 + 1LL)) << 32);
  v2 = 512;
  *MK_FP(1, v2) = v1;
  v3 = 512;
  v4 = *MK_FP(1, v3) == 0xFFFFFF7F00000180LL;
  v5 = 520;
  return v4 & (*MK_FP(1, v5) == 0xFFFF800000001234LL);
}
