// wasmdec: bounds=hidden
__int64 __usercall lane_store64@<r0:r1>(__int64 a1@<r0:r1>)
{
  __int64 v1; // kr10_8
  __int64 v4; // kr30_8
  __int64 result; // eax:ecx

  v1 = 512;
  *(_QWORD *)a1 = *MK_FP(1, v1);
  v4 = *(_QWORD *)a1;
  HIDWORD(result) = HIDWORD(*(_QWORD *)a1);
  LODWORD(result) = v4;
  return result;
}
