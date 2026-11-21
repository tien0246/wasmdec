// wasmdec: bounds=hidden
__int64 __usercall get_i64@<r0:r1>()
{
  __int64 result; // eax:ecx

  HIDWORD(result) = HIDWORD(wasm_global_1);
  LODWORD(result) = wasm_global_1;
  return result;
}
