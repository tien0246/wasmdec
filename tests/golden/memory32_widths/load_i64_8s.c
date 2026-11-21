// wasmdec: bounds=hidden
__int64 __usercall load_i64_8s@<r0:r1>(int a1@<r0>)
{
  int v3; // kr10_4
  __int64 result; // eax:ecx

  v3 = *(char *)((unsigned int)a1 + 0x123CLL);
  HIDWORD(result) = (unsigned __int64)*(char *)((unsigned int)a1 + 0x123CLL) >> 32;
  LODWORD(result) = v3;
  return result;
}
