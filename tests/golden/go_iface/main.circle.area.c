// wasmdec: bounds=hidden
// go: (main.circle) area(...) [method of main.circle (type descriptor) (method signature unrecoverable)]
int __usercall main.circle.area@<r0>(int a1@<r0>)
{
  int v1; // kr04_4

  v1 = go_sp;
  *(double *)(unsigned int)(go_sp + 16) = *(double *)&byte_2[go_sp + 6] * 3.0 * *(double *)&byte_2[go_sp + 6];
  go_sp = v1 + 8;
  return 0;
}
