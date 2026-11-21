// wasmdec: bounds=hidden
void __usercall main.wb1_near_miss(int a1@<r0>)
{
  __int64 v1; // kr40_8
  unsigned int v2; // eax
  unsigned int v3; // ecx
  unsigned int v4; // kr38_4
  __int64 v5; // [rsp+8h] [rbp-38h]
  __int64 v6; // [rsp+20h] [rbp-20h]

  v1 = 2048;
  if ( *MK_FP(1, v1) == 0 )
  {
    if ( v6 == 0 )
      ((void (__usercall *)())runtime.sigpanic)();
    __SET_PAIR__(v2, v3, runtime.gcWriteBarrier1());
    v4 = v3;
    *(_QWORD *)v4 = v5;
  }
  *(_QWORD *)(unsigned int)v6 = v5;
}
