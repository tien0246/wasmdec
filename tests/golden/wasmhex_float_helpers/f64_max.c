// wasmdec: bounds=hidden
double __usercall f64_max@<r0:r1>(double a1@<r0:r1>, double a2@<r2:r3>)
{
  unsigned int v2; // eax
  unsigned int v3; // ecx
  double v4; // kr18_8
  double v7; // kr20_8
  double result; // eax:ecx

  __SET_PAIR__(v2, v3, fmaximum(*(unsigned __int64 *)&a1, *(unsigned __int64 *)&a2));
  v7 = COERCE_DOUBLE(__PAIR64__(v2, v3));
  HIDWORD(result) = HIDWORD(v7);
  LODWORD(result) = LODWORD(v7);
  return result;
}
