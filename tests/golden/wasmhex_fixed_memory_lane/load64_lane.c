// wasmdec: bounds=hidden
__int64 __usercall load64_lane@<r0:r1>(int a1@<r0>)
{
  __int64 v1; // kr18_8
  __int64 v2; // kr10_8
  __int64 v3; // kr10_8
  __int64 result; // eax:ecx

  v1 = *(_QWORD *)((unsigned int)a1 + 8LL);
  v2 = 438;
  *MK_FP(1, v2) = v1;
  v3 = 438;
  v3 = *MK_FP(1, v3);
  HIDWORD(result) = HIDWORD(v3);
  LODWORD(result) = v3;
  return result;
}
