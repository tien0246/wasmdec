// wasmdec: bounds=hidden
double __usercall load_f64@<r0:r1>(__int64 a1@<r0:r1>)
{
  __int64 v3; // kr18_8
  double result; // eax:ecx

  v3 = *(_QWORD *)(a1 + 0x100000003LL);
  HIDWORD(result) = HIDWORD(v3);
  LODWORD(result) = v3;
  return result;
}
