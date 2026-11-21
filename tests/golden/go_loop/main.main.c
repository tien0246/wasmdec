// wasmdec: bounds=hidden
void __usercall main.main(int a1@<r0>)
{
  int v1; // [rsp+0h] [rbp-28h] BYREF
  int v2; // [rsp+8h] [rbp-20h]
  int v3; // [rsp+14h] [rbp-14h]

  v1 = 10;
  ((void (__usercall *)())main.sumSquares)();
  v3 = v2;
  runtime.printlock();
  runtime.printint(v3);
  runtime.printnl();
  runtime.printunlock();
  v1 = 100;
  ((void (__usercall *)())main.sumSquares)();
  v3 = v2;
  runtime.printlock();
  runtime.printint(v3);
  runtime.printnl();
  runtime.printunlock();
  v1 = 0;
  ((void (__usercall *)())main.sumSquares)();
  v3 = v2;
  runtime.printlock();
  runtime.printint(v3);
  runtime.printnl();
  runtime.printunlock();
}
