// wasmdec: bounds=hidden
double __usercall get_f64@<r0:r1>()
{
  double v2; // kr08_8
  double result; // eax:ecx

  v2 = wasm_global_3;
  HIDWORD(result) = HIDWORD(v2);
  LODWORD(result) = LODWORD(v2);
  return result;
}
