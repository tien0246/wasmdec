// wasmdec: bounds=hidden
double __usercall f64_convert_i64_u@<r0:r1>(__int64 a1@<r0:r1>)
{
  double v3; // kr10_8
  double result; // eax:ecx

  v3 = (double)(unsigned __int64)a1;
  HIDWORD(result) = HIDWORD(v3);
  LODWORD(result) = LODWORD(v3);
  return result;
}
