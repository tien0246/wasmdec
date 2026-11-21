// wasmdec: bounds=hidden
int __usercall store32_lane@<r0>(int a1@<r0>)
{
  __int64 v1; // kr08_8

  v1 = 512;
  *(_DWORD *)((unsigned int)a1 + 4LL) = HIDWORD(*MK_FP(1, v1));
  return *(_DWORD *)((unsigned int)a1 + 4LL);
}
