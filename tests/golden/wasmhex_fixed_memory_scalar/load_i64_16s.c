// wasmdec: bounds=hidden
__int64 __usercall load_i64_16s@<r0:r1>(int a1@<r0>)
{
  int v3; // kr10_4
  __int64 result; // eax:ecx

  v3 = *(__int16 *)((unsigned int)a1 + 0x80LL);
  HIDWORD(result) = (unsigned __int64)*(__int16 *)((unsigned int)a1 + 0x80LL) >> 32;
  LODWORD(result) = v3;
  return result;
}
