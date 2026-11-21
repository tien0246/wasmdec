// wasmdec: bounds=hidden
__int64 __usercall load_i64_32u@<r0:r1>(__int64 a1@<r0:r1>)
{
  int v3; // kr20_4
  __int64 result; // eax:ecx

  v3 = *(_DWORD *)(a1 + 0x10000000DLL);
  HIDWORD(result) = 0;
  LODWORD(result) = v3;
  return result;
}
