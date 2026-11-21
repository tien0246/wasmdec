// wasmdec: bounds=hidden
__int64 __usercall load_i64_8s@<r0:r1>(__int64 a1@<r0:r1>)
{
  int v3; // kr20_4
  __int64 result; // eax:ecx

  v3 = *(char *)(a1 + 0x100000008LL);
  HIDWORD(result) = (unsigned __int64)*(char *)(a1 + 0x100000008LL) >> 32;
  LODWORD(result) = v3;
  return result;
}
