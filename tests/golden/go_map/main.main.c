// wasmdec: bounds=hidden
void __usercall main.main(int a1@<r0>)
{
  __int64 *v1; // [rsp+0h] [rbp-140h] BYREF
  go_agg16 v2; // [rsp+8h] [rbp-138h]
  int v3; // [rsp+18h] [rbp-128h]
  int v4; // [rsp+20h] [rbp-120h]
  int v5; // [rsp+24h] [rbp-11Ch]
  __int64 v6; // [rsp+28h] [rbp-118h]
  __int64 v7; // [rsp+30h] [rbp-110h]
  _QWORD v8[25]; // [rsp+38h] [rbp-108h] BYREF
  __int64 v9[8]; // [rsp+100h] [rbp-40h] BYREF

  memset((unsigned int)v9, 0, 0x30u);
  memset((unsigned int)v8, 0, sizeof(v8));
  v8[0] = 0x8080808080808080LL;
  ((void (__usercall *)(__int64@<r0:r1>))runtime.rand)((__int64)v8);
  v9[1] = (__int64)v1;
  v1 = v9;
  ((void (__usercall *)(__int64, go_agg16))main.bump)((__int64)v9, "alpha");
  v4 = v3;
  v1 = v9;
  ((void (__usercall *)(__int64, go_agg16))main.bump)((__int64)v9, "beta");
  v5 = v3;
  v1 = v9;
  ((void (__usercall *)(__int64, go_agg16))main.bump)((__int64)v9, "alpha");
  v7 = v4 + v5 + v3;
  ((void (__usercall *)(__int64@<r0:r1>))runtime.printlock)(v9[0]);
  runtime.printint(v7);
  ((void (__usercall *)())runtime.printsp)();
  runtime.printint(v6);
  runtime.printnl();
  runtime.printunlock();
}
