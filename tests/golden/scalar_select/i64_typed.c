// wasmdec: bounds=hidden
__int64 __usercall i64_typed@<r0:r1>(__int64 a1@<r0:r1>, __int64 a2@<r2:r3>, int a3@<r6>)
{
  unsigned int v3; // eax
  unsigned int v4; // ecx
  unsigned __int64 v5; // kr18_8
  __int64 result; // eax:ecx

  __SET_PAIR__(v3, v4, a3 ? a1 : a2);
  v5 = __PAIR64__(v3, v4);
  HIDWORD(result) = HIDWORD(v5);
  LODWORD(result) = v5;
  return result;
}
