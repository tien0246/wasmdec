// wasmdec: bounds=hidden
int __usercall store8_lane@<r0>(int a1@<r0>)
{
  __int64 v1; // kr08_8

  v1 = 512;
  *(_BYTE *)((unsigned int)a1 + 1LL) = HIBYTE(*MK_FP(1, v1));
  return *(unsigned __int8 *)((unsigned int)a1 + 1LL);
}
