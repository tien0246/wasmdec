// wasmdec: bounds=hidden
__int64 __usercall i64_trunc_f32_u_bits@<r0:r1>(int a1@<r0>)
{
  float v1; // kr04_4
  unsigned int v2; // eax
  unsigned int v3; // ecx
  unsigned __int64 v4; // kr08_8
  __int64 result; // eax:ecx

  v1 = COERCE_FLOAT((unsigned int)a1);
  __SET_PAIR__(v2, v3, wasm_trap_i64_trunc_f32_u(v1));
  v4 = __PAIR64__(v2, v3);
  HIDWORD(result) = HIDWORD(v4);
  LODWORD(result) = v4;
  return result;
}
