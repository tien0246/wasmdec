// wasmdec: bounds=hidden
double __usercall f64_reinterpret_i64@<r0:r1>(__int64 a1@<r0:r1>)
{
  unsigned int v1; // eax
  unsigned int v2; // ecx
  double v3; // kr08_8
  double v6; // kr10_8
  double result; // eax:ecx

  __SET_PAIR__(v1, v2, COERCE_DOUBLE((unsigned __int64)a1));
  v6 = COERCE_DOUBLE(__PAIR64__(v1, v2));
  HIDWORD(result) = HIDWORD(v6);
  LODWORD(result) = LODWORD(v6);
  return result;
}
