// wasmdec: bounds=hidden
double __usercall float_ops@<r0:r1>(double a1@<r0:r1>)
{
  unsigned int v1; // kr10_4
  double v2; // kr40_8
  double v3; // kr70_8
  double v6; // kr18_8
  double result; // eax:ecx

  v1 = wasm_global_0 - 16;
  *(double *)((unsigned int)(wasm_global_0 - 16) + 8LL) = a1;
  if ( *(double *)(v1 + 8LL) >= (double)0 )
  {
    if ( *(double *)(v1 + 8LL) <= 100.0 )
      v3 = *(double *)(v1 + 8LL);
    else
      v3 = 100.0;
    v2 = v3;
  }
  else
  {
    v2 = (double)0;
  }
  *(double *)v1 = v2;
  v6 = *(double *)v1 * 2.0;
  HIDWORD(result) = HIDWORD(v6);
  LODWORD(result) = LODWORD(v6);
  return result;
}
