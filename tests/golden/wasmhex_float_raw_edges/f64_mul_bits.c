// wasmdec: bounds=hidden
__int64 __usercall f64_mul_bits@<r0:r1>(__int64 a1@<r0:r1>, __int64 a2@<r2:r3>)
{
  double v2; // kr18_8
  double v3; // kr20_8
  __int64 v4; // kr10_8
  __int64 result; // eax:ecx
  double v8; // 0:ecx.4,4:eax.4
  double v9; // 0:ecx.4,4:eax.4
  __int64 v10; // 0:ecx.4,4:eax.4

  v2 = COERCE_DOUBLE((unsigned __int64)a1);
  v3 = COERCE_DOUBLE((unsigned __int64)a2);
  v4 = (__int64)COERCE_UNSIGNED_INT64(v2 * v3);
  HIDWORD(result) = HIDWORD(v4);
  LODWORD(result) = v4;
  return result;
}
