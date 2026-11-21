// wasmdec: bounds=hidden
double __usercall f64_add@<r0:r1>(double a1@<r0:r1>)
{
  double v3; // kr10_8
  double result; // eax:ecx

  v3 = a1 + 1.0;
  HIDWORD(result) = HIDWORD(v3);
  LODWORD(result) = LODWORD(v3);
  return result;
}
