// wasmhex: bounds=hidden
__int64 __usercall target_mixed@<r0:r1>(int a1@<r0>, __int64 a2@<r1:r2>, float a3@<r3>, double a4@<r6:r7>)
{
  __int64 result; // eax:ecx

  HIDWORD(result) = HIDWORD(a2);
  LODWORD(result) = a2;
  return result;
}
