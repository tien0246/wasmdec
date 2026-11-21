// wasmdec: bounds=hidden
double __usercall f64_floor@<r0:r1>(double a1@<r0:r1>)
{
  unsigned int v1; // eax
  unsigned int v2; // ecx
  double v3; // kr10_8
  double v6; // kr18_8
  double result; // eax:ecx

  __SET_PAIR__(v1, v2, floor(*(unsigned __int64 *)&a1));
  v6 = COERCE_DOUBLE(__PAIR64__(v1, v2));
  HIDWORD(result) = HIDWORD(v6);
  LODWORD(result) = LODWORD(v6);
  return result;
}
