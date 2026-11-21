// wasmdec: bounds=hidden
__int64 __usercall i64_trunc_f64_u_bits@<r0:r1>(__int64 a1@<r0:r1>)
{
  double v1; // kr10_8
  unsigned int v2; // eax
  unsigned int v3; // ecx
  double v4; // kr08_8
  __int64 result; // eax:ecx
  double v8; // 0:ecx.4,4:eax.4

  v8 = COERCE_DOUBLE((unsigned __int64)a1);
  v1 = v8;
  __SET_PAIR__(v2, v3, wasm_trap_i64_trunc_f64_u(v1));
  v4 = v8;
  HIDWORD(result) = HIDWORD(v4);
  LODWORD(result) = LODWORD(v4);
  return result;
}
