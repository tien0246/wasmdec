// wasmdec: bounds=hidden
__int64 __usercall runtime.gcWriteBarrier1@<r0:r1>()
{
  unsigned int v0; // eax
  unsigned int v1; // ecx
  unsigned __int64 v2; // kr00_8
  __int64 result; // eax:ecx

  __SET_PAIR__(v0, v1, wasm_func_6(8));
  v2 = __PAIR64__(v0, v1);
  HIDWORD(result) = HIDWORD(v2);
  LODWORD(result) = v2;
  return result;
}
