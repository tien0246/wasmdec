// wasmdec: bounds=hidden
int __usercall i32_sat_f64_s_bits@<r0>(__int64 a1@<r0:r1>)
{
  unsigned int v1; // eax
  unsigned int v2; // ecx
  unsigned __int64 v3; // kr10_8

  __SET_PAIR__(v1, v2, COERCE_DOUBLE((unsigned __int64)a1));
  *(double *)&v3 = COERCE_DOUBLE(__PAIR64__(v1, v2));
  return trunc_sat_i32_s(v3);
}
