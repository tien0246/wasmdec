// wasmdec: bounds=hidden
__int64 __usercall load_i64_16u@<r0:r1>(int a1@<r0>)
{
  int v3; // kr10_4
  __int64 result; // eax:ecx

  v3 = *(unsigned __int16 *)((unsigned int)a1 + 0x123FLL);
  HIDWORD(result) = 0;
  LODWORD(result) = v3;
  return result;
}
