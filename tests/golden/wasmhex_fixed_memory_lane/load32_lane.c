// wasmdec: bounds=hidden
int __usercall load32_lane@<r0>(int a1@<r0>)
{
  unsigned __int8 v1; // kr08_1
  unsigned int v2; // kr20_4
  unsigned __int64 v3; // kr58_8
  __int64 v4; // kr08_8
  __int64 v5; // kr08_8

  v1 = 0;
  v2 = *(_DWORD *)((unsigned int)a1 + 4LL);
  v3 = (unsigned int)*MK_FP(1, v1 + 8LL) | ((unsigned __int64)v2 << 32);
  v4 = 424;
  *MK_FP(1, v4) = HIDWORD(v3);
  v5 = 424;
  return *MK_FP(1, v5);
}
