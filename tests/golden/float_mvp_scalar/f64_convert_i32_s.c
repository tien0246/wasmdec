// wasmdec: bounds=hidden
double __usercall f64_convert_i32_s@<r0:r1>(int a1@<r0>)
{
  double v3; // kr10_8
  double result; // eax:ecx

  v3 = (double)a1;
  HIDWORD(result) = HIDWORD(v3);
  LODWORD(result) = LODWORD(v3);
  return result;
}
