// wasmdec: bounds=hidden
__int64 __usercall store64_lane@<r0:r1>(int a1@<r0>)
{
  __int64 v1; // kr10_8
  __int64 v4; // kr10_8
  __int64 result; // eax:ecx

  v1 = 512;
  *(_QWORD *)((unsigned int)a1 + 8LL) = *MK_FP(1, v1 + 8);
  v4 = *(_QWORD *)((unsigned int)a1 + 8LL);
  HIDWORD(result) = HIDWORD(v4);
  LODWORD(result) = v4;
  return result;
}
