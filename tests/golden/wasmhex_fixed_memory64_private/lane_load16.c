// wasmdec: bounds=hidden
int __usercall lane_load16@<r0>(__int64 a1@<r0:r1>)
{
  __int64 v1; // kr10_8
  unsigned __int16 v2; // kr0C_2
  __int128 v3; // kr50_16
  __int64 v4; // kr10_8
  __int64 v5; // kr10_8

  v1 = 512;
  v2 = *(_WORD *)a1;
  *(_QWORD *)&v3 = *MK_FP(1, v1) & 0xFFFFFFFFFFFF0000LL | v2;
  *((_QWORD *)&v3 + 1) = *MK_FP(1, v1 + 8);
  v4 = 4096;
  *MK_FP(1, v4) = v3;
  v5 = 4096;
  return *MK_FP(1, v5);
}
