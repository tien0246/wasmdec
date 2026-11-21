// wasmdec: bounds=hidden
double __usercall load_f64@<r0:r1>(int a1@<r0>)
{
  __int64 v3; // kr10_8
  double result; // eax:ecx

  v3 = *(_QWORD *)((unsigned int)a1 + 0x1237LL);
  HIDWORD(result) = HIDWORD(v3);
  LODWORD(result) = v3;
  return result;
}
