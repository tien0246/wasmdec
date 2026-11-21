// wasmdec: bounds=hidden
__int64 __usercall f64_nearest_bits@<r0:r1>(__int64 a1@<r0:r1>)
{
  unsigned __int64 v1; // kr10_8
  unsigned int v2; // eax
  double v3; // kr08_8
  unsigned __int64 v4; // kr08_8
  __int64 result; // eax:ecx
  double v8; // 0:ecx.4,4:eax.4
  __int64 v9; // 0:ecx.4,4:eax.4

  v8 = COERCE_DOUBLE((unsigned __int64)a1);
  *(double *)&v1 = v8;
  __SET_PAIR__(v2, v9, roundeven(v1));
  v3 = v8;
  v9 = (__int64)COERCE_UNSIGNED_INT64(v3);
  v4 = v9;
  HIDWORD(result) = HIDWORD(v4);
  LODWORD(result) = v4;
  return result;
}
