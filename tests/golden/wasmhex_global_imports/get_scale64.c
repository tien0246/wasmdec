// wasmdec: bounds=hidden
double __usercall get_scale64@<r0:r1>()
{
  double v2; // kr08_8
  double result; // eax:ecx

  v2 = scale64;
  HIDWORD(result) = HIDWORD(v2);
  LODWORD(result) = LODWORD(v2);
  return result;
}
