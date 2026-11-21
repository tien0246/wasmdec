// wasmdec: bounds=hidden
__int64 __usercall i64_sat_f32_s_bits@<r0:r1>(int a1@<r0>)
{
  unsigned int v1; // kr04_4
  unsigned int v2; // eax
  unsigned int v3; // ecx
  unsigned __int64 v4; // kr08_8
  __int64 result; // eax:ecx

  *(float *)&v1 = COERCE_FLOAT((unsigned int)a1);
  __SET_PAIR__(v2, v3, trunc_sat_i64_s(v1));
  v4 = __PAIR64__(v2, v3);
  HIDWORD(result) = HIDWORD(v4);
  LODWORD(result) = v4;
  return result;
}
