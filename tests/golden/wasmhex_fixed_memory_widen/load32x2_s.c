// wasmdec: bounds=hidden
int __usercall load32x2_s@<r0>(int a1@<r0>)
{
  __int128 v1; // kr40_16
  __int64 v2; // kr08_8
  __int64 v3; // kr08_8
  bool v4; // kr04_1
  __int64 v5; // kr08_8

  *(_QWORD *)&v1 = (int)*(_QWORD *)((unsigned int)a1 + 1LL);
  *((_QWORD *)&v1 + 1) = (int)HIDWORD(*(_QWORD *)((unsigned int)a1 + 1LL));
  v2 = 512;
  *MK_FP(1, v2) = v1;
  v3 = 512;
  v4 = *MK_FP(1, v3) == -8453760;
  v5 = 520;
  return v4 & (*MK_FP(1, v5) == -2147478988);
}
