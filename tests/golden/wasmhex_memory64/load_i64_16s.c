// wasmdec: bounds=hidden
__int64 __usercall load_i64_16s@<r0:r1>(__int64 a1@<r0:r1>)
{
  int v3; // kr20_4
  __int64 result; // eax:ecx

  v3 = *(__int16 *)(a1 + 0x10000000ALL);
  HIDWORD(result) = (unsigned __int64)*(__int16 *)(a1 + 0x10000000ALL) >> 32;
  LODWORD(result) = v3;
  return result;
}
