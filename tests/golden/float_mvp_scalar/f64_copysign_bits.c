// wasmdec: bounds=hidden
__int64 __usercall f64_copysign_bits@<r0:r1>(__int64 a1@<r0:r1>, __int64 a2@<r2:r3>)
{
  unsigned __int64 v2; // kr18_8
  unsigned __int64 v3; // kr20_8
  unsigned int v4; // eax
  double v5; // kr10_8
  unsigned __int64 v6; // kr10_8
  __int64 result; // eax:ecx
  double v10; // 0:ecx.4,4:eax.4
  double v11; // 0:ecx.4,4:eax.4
  __int64 v12; // 0:ecx.4,4:eax.4

  v10 = COERCE_DOUBLE((unsigned __int64)a1);
  *(double *)&v2 = v10;
  v11 = COERCE_DOUBLE((unsigned __int64)a2);
  *(double *)&v3 = v11;
  __SET_PAIR__(v4, v12, copysign(v2, v3));
  v5 = v11;
  v12 = (__int64)COERCE_UNSIGNED_INT64(v5);
  v6 = v12;
  HIDWORD(result) = HIDWORD(v6);
  LODWORD(result) = v6;
  return result;
}
