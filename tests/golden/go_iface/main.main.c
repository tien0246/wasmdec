// wasmdec: bounds=hidden
int __usercall main.main@<r0>(int a1@<r0>)
{
  int v1; // kr04_4
  int v2; // kr04_4

  v1 = go_sp;
  v2 = v1 - 32;
  go_sp = v2;
  main.areaOf(v2);
  ((void (__usercall *)(double@<r0:r1>))runtime.printlock)(*(double *)(unsigned int)(v2 + 16));
  ((void (__usercall *)(double@<r0:r1>))runtime.printfloat64)(*(double *)&byte_2[v2 + 22]);
  runtime.printnl();
  runtime.printunlock();
  main.areaOf(v2);
  ((void (__usercall *)(double@<r0:r1>))runtime.printlock)(*(double *)(unsigned int)(v2 + 16));
  ((void (__usercall *)(double@<r0:r1>))runtime.printfloat64)(*(double *)&byte_2[v2 + 22]);
  runtime.printnl();
  runtime.printunlock();
  go_sp = v2 + 40;
  return 0;
}
