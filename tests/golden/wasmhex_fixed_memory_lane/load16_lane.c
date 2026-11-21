// wasmdec: bounds=hidden
int __usercall load16_lane@<r0>(int a1@<r0>)
{
  unsigned __int8 v1; // kr08_1
  unsigned __int16 v2; // kr20_2
  unsigned __int64 v3; // kr50_8
  __int64 v4; // kr08_8
  __int64 v5; // kr08_8

  v1 = 0;
  v2 = *(_WORD *)((unsigned int)a1 + 2LL);
  v3 = *MK_FP(1, v1) & 0xFFFFFFFFFFFF0000LL | v2;
  v4 = 412;
  *MK_FP(1, v4) = v3;
  v5 = 412;
  return (unsigned __int16)*MK_FP(1, v5);
}
