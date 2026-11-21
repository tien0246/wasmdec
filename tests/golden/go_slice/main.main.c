// wasmdec: bounds=hidden
void __usercall main.main(int a1@<r0>)
{
  __int64 v1; // kr08_8
  __int64 v2; // kr10_8
  go_slice v3; // [rsp+0h] [rbp-70h] BYREF
  int v4; // [rsp+18h] [rbp-58h]
  __int64 v5; // [rsp+28h] [rbp-48h]
  __int64 v6; // [rsp+38h] [rbp-38h]
  _QWORD v7[2]; // [rsp+44h] [rbp-2Ch] BYREF
  int v8; // [rsp+54h] [rbp-1Ch]
  __int64 v9; // [rsp+58h] [rbp-18h]

  v7[0] = 0x140000000ALL;
  v7[1] = 0x280000001ELL;
  v3.ptr = (__int64)v7;
  v3.len = 4;
  v3.cap = 4;
  ((void (__usercall *)(go_slice))main.sumSlice)(v3);
  v8 = v4;
  runtime.printlock();
  runtime.printint(v8);
  runtime.printnl();
  runtime.printunlock();
  runtime.growslice((__int64)v7, 5, 4, 1, 868144);
  v1 = v5;
  v2 = v6;
  v9 = v6;
  *(_DWORD *)&byte_2[(unsigned int)v5 + 14] = 50;
  v3.ptr = v1;
  v3.len = 5;
  v3.cap = v2;
  ((void (__usercall *)(go_slice))main.sumSlice)(v3);
  v8 = v4;
  runtime.printlock();
  runtime.printint(v8);
  ((void (__usercall *)())runtime.printsp)();
  runtime.printint(5);
  ((void (__usercall *)())runtime.printsp)();
  runtime.printint(v9);
  runtime.printnl();
  runtime.printunlock();
}
/* Orphan comments:
go: box(int32)
*/
