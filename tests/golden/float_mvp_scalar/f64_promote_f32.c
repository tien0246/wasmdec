// wasmdec: bounds=hidden
double __usercall f64_promote_f32@<r0:r1>(float a1@<r0>)
{
  double v3; // kr10_8
  double result; // eax:ecx

  v3 = a1;
  HIDWORD(result) = HIDWORD(v3);
  LODWORD(result) = LODWORD(v3);
  return result;
}
