// wasmdec: bounds=hidden
__int64 __usercall table_size_fixed64@<r0:r1>()
{
  __int64 result; // eax:ecx

  HIDWORD(result) = 0;
  LODWORD(result) = 9;
  return result;
}
