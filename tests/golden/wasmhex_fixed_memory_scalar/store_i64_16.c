// wasmdec: bounds=hidden
__int64 __usercall store_i64_16@<r0:r1>(int a1@<r0>, __int64 a2@<r1:r2>)
{
  int v4; // kr18_4
  __int64 result; // eax:ecx

  *(_WORD *)((unsigned int)a1 + 0x100LL) = a2;
  v4 = *(unsigned __int16 *)((unsigned int)a1 + 0x100LL);
  HIDWORD(result) = 0;
  LODWORD(result) = v4;
  return result;
}
