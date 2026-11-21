// wasmdec: bounds=hidden
double __usercall store_f64@<r0:r1>(int a1@<r0>, double a2@<r1:r2>)
{
  __int64 v4; // kr18_8
  double result; // eax:ecx

  *(double *)((unsigned int)a1 + 0x100LL) = a2;
  v4 = *(_QWORD *)((unsigned int)a1 + 0x100LL);
  HIDWORD(result) = HIDWORD(v4);
  LODWORD(result) = v4;
  return result;
}
