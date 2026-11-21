// wasmdec: bounds=hidden
__int64 __usercall wasm_func_1@<r0:r1>()
{
  __int64 result; // eax:ecx

  HIDWORD(result) = 0;
  LODWORD(result) = 512;
  return result;
}
