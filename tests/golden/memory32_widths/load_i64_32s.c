// wasmdec: bounds=hidden
__int64 __usercall load_i64_32s@<r0:r1>(int a1@<r0>)
{
  int v3; // kr10_4
  __int64 result; // eax:ecx

  v3 = *(_DWORD *)((unsigned int)a1 + 0x1240LL);
  HIDWORD(result) = v3 >> 31;
  LODWORD(result) = v3;
  return result;
}
