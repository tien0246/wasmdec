// wasmhex: bounds=hidden
__int64 __usercall target_i64@<r0:r1>(__int64 a1@<r0:r1>)
{
  __int64 result; // eax:ecx

  HIDWORD(result) = (unsigned __int64)(a1 + 1) >> 32;
  LODWORD(result) = a1 + 1;
  return result;
}
