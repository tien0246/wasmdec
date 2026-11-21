// wasmdec: bounds=hidden
double __usercall float_ops@<r0:r1>(double a1@<r0:r1>)
{
  double v1; // kr00_8
  double v2; // kr20_8
  double v3; // kr10_8
  double v6; // kr08_8
  double result; // eax:ecx
  double v8; // 0:ecx.4,4:eax.4
  double v9; // 0:ecx.4,4:eax.4

  v1 = a1;
  v2 = a1 > 100.0 ? 100.0 : a1;
  v6 = v1 < 0.0 ? 0.0 : v2 + v2;
  HIDWORD(result) = HIDWORD(v6);
  LODWORD(result) = LODWORD(v6);
  return result;
}
