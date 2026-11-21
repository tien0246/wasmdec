// wasmdec: bounds=hidden
__int64 __usercall f64_from_i64_u_bits@<r0:r1>(__int64 a1@<r0:r1>)
{
  unsigned int v1; // eax
  unsigned int v2; // ecx
  unsigned __int64 v3; // kr08_8
  __int64 result; // eax:ecx

  __SET_PAIR__(v1, v2, (__int64)COERCE_UNSIGNED_INT64((double)(unsigned __int64)a1));
  v3 = __PAIR64__(v1, v2);
  HIDWORD(result) = HIDWORD(v3);
  LODWORD(result) = v3;
  return result;
}
