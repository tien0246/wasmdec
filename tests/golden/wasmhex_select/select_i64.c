// wasmdec: bounds=hidden
// wasmdec ICF-folded with: select_i32 select_f32 select_f64 (+0 more)
__int64 __usercall select_i64@<r0:r1>(int a1@<r0>, __int64 a2@<r1:r2>, __int64 a3@<r3:r6>)
{
  unsigned int v3; // eax
  unsigned int v4; // ecx
  unsigned __int64 v5; // kr18_8
  __int64 result; // eax:ecx

  __SET_PAIR__(v3, v4, a1 ? a2 : a3);
  v5 = __PAIR64__(v3, v4);
  HIDWORD(result) = HIDWORD(v5);
  LODWORD(result) = v5;
  return result;
}
