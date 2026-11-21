// wasmdec: bounds=hidden
int __usercall load8_lane@<r0>(int a1@<r0>)
{
  unsigned __int8 v1; // kr08_1
  unsigned __int8 v2; // kr20_1
  unsigned __int64 v3; // kr58_8
  __int64 v4; // kr08_8
  __int64 v5; // kr08_8

  v1 = 0;
  v2 = *(_BYTE *)((unsigned int)a1 + 1LL);
  v3 = *MK_FP(1, v1 + 8LL) & 0xFFFFFFFFFFFFFFLL | ((unsigned __int64)v2 << 56);
  v4 = 401;
  *MK_FP(1, v4) = HIBYTE(v3);
  v5 = 401;
  return (unsigned __int8)*MK_FP(1, v5);
}
