// wasmdec: bounds=hidden
__int64 __usercall conditional_return_i64@<r0:r1>(int a1@<r0>, __int64 a2@<r1:r2>)
{
  __int64 result; // eax:ecx

  HIDWORD(result) = 0;
  LODWORD(result) = 7;
  return result;
}
