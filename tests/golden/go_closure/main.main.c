// wasmdec: bounds=hidden
void __usercall main.main(int a1@<r0>)
{
  __int64 v1; // kr08_8
  void (__usercall *v2)(); // kr10_8
  __int64 v3; // kr08_8
  void (__usercall *v4)(); // kr10_8
  int v5; // [rsp+0h] [rbp-28h] BYREF
  __int64 v6; // [rsp+8h] [rbp-20h]
  int v7; // [rsp+14h] [rbp-14h]

  v5 = 7;
  ((void (__usercall *)())main.adder)();
  v1 = v6;
  if ( v6 == 0 )
    ((void (__usercall *)())runtime.sigpanic)();
  v2 = *(void (__usercall **)())(unsigned int)v1;
  v5 = 5;
  go_ctxt = v1;
  v2();
  v7 = v6;
  runtime.printlock();
  runtime.printint(v7);
  runtime.printnl();
  runtime.printunlock();
  v5 = -1;
  ((void (__usercall *)())main.adder)();
  v3 = v6;
  if ( v6 == 0 )
    ((void (__usercall *)())runtime.sigpanic)();
  v4 = *(void (__usercall **)())(unsigned int)v3;
  v5 = 10;
  go_ctxt = v3;
  v4();
  v7 = v6;
  runtime.printlock();
  runtime.printint(v7);
  runtime.printnl();
  runtime.printunlock();
}
