// wasmdec: bounds=hidden
__int64 __usercall div_s@<r0:r1>(__int64 a1@<r0:r1>, __int64 a2@<r2:r3>)
{
  unsigned int v2; // eax
  unsigned int v3; // ecx
  unsigned __int64 v4; // kr10_8
  __int64 result; // eax:ecx

  __SET_PAIR__(v2, v3, a1 / a2);
  v4 = __PAIR64__(v2, v3);
  HIDWORD(result) = HIDWORD(v4);
  LODWORD(result) = v4;
  return result;
}
