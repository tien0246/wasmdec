// wasmdec: bounds=hidden
__int64 __usercall scalar_i64_load16_s@<r0:r1>(__int64 a1@<r0:r1>)
{
  int v3; // kr18_4
  __int64 result; // eax:ecx

  v3 = *(__int16 *)a1;
  HIDWORD(result) = (unsigned __int64)*(__int16 *)a1 >> 32;
  LODWORD(result) = v3;
  return result;
}
