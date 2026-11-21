// wasmdec: bounds=hidden
int __usercall load32_zero@<r0>(int a1@<r0>)
{
  unsigned __int8 v1; // kr08_1
  unsigned __int8 v2; // kr08_1
  bool v3; // kr04_1
  __int64 v4; // kr08_8

  v1 = 0;
  *MK_FP(1, v1) = *(unsigned int *)((unsigned int)a1 + 0x10LL);
  v2 = 0;
  v3 = *MK_FP(1, v2) == 305419896;
  v4 = 8;
  return v3 & (*MK_FP(1, v4) == 0);
}
