// wasmdec: bounds=hidden
__int64 __usercall zero_i64_high@<r0:r1>(__int64 a1@<r0:r1>)
{
  __int64 v1; // kr18_8
  __int64 v2; // kr18_8
  __int64 result; // eax:ecx

  v1 = 4096;
  *MK_FP(1, v1) = *(unsigned __int64 *)a1;
  v2 = 4104;
  HIDWORD(result) = HIDWORD(*MK_FP(1, v2));
  LODWORD(result) = *MK_FP(1, v2);
  return result;
}
