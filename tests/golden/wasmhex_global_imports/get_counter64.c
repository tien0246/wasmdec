// wasmdec: bounds=hidden
__int64 __usercall get_counter64@<r0:r1>()
{
  __int64 result; // eax:ecx

  HIDWORD(result) = HIDWORD(counter64);
  LODWORD(result) = counter64;
  return result;
}
