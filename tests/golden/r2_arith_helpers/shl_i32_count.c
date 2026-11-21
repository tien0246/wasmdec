// wasmdec: bounds=hidden
__int64 __usercall shl_i32_count@<r0:r1>(__int64 a1@<r0:r1>, int a2@<r2>)
{
  unsigned int v2; // eax
  unsigned int v3; // ecx
  unsigned __int64 v4; // kr10_8
  __int64 result; // eax:ecx

  __SET_PAIR__(v2, v3, (__int64)((unsigned __int64)a1 << ((unsigned __int64)(unsigned int)a2 & 0x3FuLL)));
  v4 = __PAIR64__(v2, v3);
  HIDWORD(result) = HIDWORD(v4);
  LODWORD(result) = v4;
  return result;
}
