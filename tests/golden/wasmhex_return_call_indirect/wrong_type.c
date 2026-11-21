// wasmdec: bounds=hidden
__int64 __usercall wrong_type@<r0:r1>(int a1@<r0>)
{
  __int64 result; // eax:ecx

  HIDWORD(result) = a1 >> 31;
  LODWORD(result) = a1;
  return result;
}
