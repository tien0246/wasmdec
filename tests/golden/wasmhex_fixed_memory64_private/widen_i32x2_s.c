// wasmdec: bounds=hidden
__int64 __usercall widen_i32x2_s@<r0:r1>(__int64 a1@<r0:r1>)
{
  __int128 v1; // kr40_16
  __int64 v2; // kr50_8
  __int64 v3; // kr50_8
  __int64 result; // eax:ecx

  *(_QWORD *)&v1 = (int)*(_QWORD *)a1;
  *((_QWORD *)&v1 + 1) = (int)HIDWORD(*(_QWORD *)a1);
  v2 = 4096;
  *MK_FP(1, v2) = v1;
  v3 = 4096;
  HIDWORD(result) = HIDWORD(*MK_FP(1, v3));
  LODWORD(result) = *MK_FP(1, v3);
  return result;
}
