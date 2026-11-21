// wasmdec: bounds=hidden
int __usercall store16_lane@<r0>(int a1@<r0>)
{
  __int64 v1; // kr08_8

  v1 = 512;
  *(_WORD *)((unsigned int)a1 + 2LL) = HIWORD(*MK_FP(1, v1));
  return *(unsigned __int16 *)((unsigned int)a1 + 2LL);
}
