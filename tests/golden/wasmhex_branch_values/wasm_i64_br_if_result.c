// wasmdec: bounds=hidden
__int64 __usercall wasm_i64_br_if_result@<r0:r1>(int a1@<r0>)
{
  __int64 v1; // kr08_8
  __int64 result; // eax:ecx

  v1 = 44;
  if ( a1 == 0 )
    v1 = 46;
  HIDWORD(result) = HIDWORD(v1);
  LODWORD(result) = v1;
  return result;
}
