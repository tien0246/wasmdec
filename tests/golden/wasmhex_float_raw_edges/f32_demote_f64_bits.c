// wasmdec: bounds=hidden
int __usercall f32_demote_f64_bits@<r0>(__int64 a1@<r0:r1>)
{
  unsigned int v1; // eax
  unsigned int v2; // ecx
  double v3; // kr10_8
  float v4; // kr0C_4

  __SET_PAIR__(v1, v2, COERCE_DOUBLE((unsigned __int64)a1));
  v4 = COERCE_DOUBLE(__PAIR64__(v1, v2));
  return (int)LODWORD(v4);
}
