// wasmdec: bounds=hidden
__int64 __usercall lane_load64@<r0:r1>(__int64 a1@<r0:r1>)
{
  __int64 v1; // kr10_8
  __int64 v2; // kr18_8
  __int128 v3; // kr50_16
  __int64 v4; // kr10_8
  __int64 v5; // kr10_8
  __int64 result; // eax:ecx

  v1 = 512;
  v2 = *(_QWORD *)a1;
  *(_QWORD *)&v3 = v2;
  *((_QWORD *)&v3 + 1) = *MK_FP(1, v1 + 8);
  v4 = 4096;
  *MK_FP(1, v4) = v3;
  v5 = 4096;
  HIDWORD(result) = HIDWORD(*MK_FP(1, v5));
  LODWORD(result) = *MK_FP(1, v5);
  return result;
}
