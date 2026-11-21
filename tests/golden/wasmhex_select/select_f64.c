// wasmdec: bounds=hidden
// wasmdec ICF-folded with: select_i32 select_i64 select_f32 (+0 more)
double __usercall select_f64@<r0:r1>(int a1@<r0>, double a2@<r1:r2>, double a3@<r3:r6>)
{
  unsigned int v3; // eax
  unsigned int v4; // ecx
  double v5; // kr18_8
  double v8; // kr20_8
  double result; // eax:ecx

  __SET_PAIR__(v3, v4, a1 ? a2 : a3);
  v8 = COERCE_DOUBLE(__PAIR64__(v3, v4));
  HIDWORD(result) = HIDWORD(v8);
  LODWORD(result) = LODWORD(v8);
  return result;
}
