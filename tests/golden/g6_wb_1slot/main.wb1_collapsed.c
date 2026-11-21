// wasmdec: bounds=hidden
void __usercall main.wb1_collapsed(int a1@<r0>)
{
  __int64 v1; // kr40_8
  __int64 v5; // [rsp+8h] [rbp-38h]
  __int64 v6; // [rsp+20h] [rbp-20h]

  v1 = 2048;
  if ( v6 == 0 )
    ((void (__usercall *)())runtime.sigpanic)();
  *(_QWORD *)(unsigned int)v6 = v5;             // go: write barrier
}
