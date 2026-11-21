// wasmdec: bounds=hidden
__int64 __usercall memory_pages64@<r0:r1>()
{
  __int64 result; // eax:ecx

  HIDWORD(result) = 0;
  LODWORD(result) = 3;
  return result;
}
