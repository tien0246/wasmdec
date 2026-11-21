// wasmdec: bounds=hidden
// go: (main.square) area(...) [method of main.square (type descriptor) (method signature unrecoverable)]
int __usercall main.square.area@<r0>(int a1@<r0>)
{
  int v1; // kr04_4

  v1 = go_sp;
  *(double *)(unsigned int)(go_sp + 16) = *(double *)&byte_2[go_sp + 6] * *(double *)&byte_2[go_sp + 6];
  go_sp = v1 + 8;
  return 0;
}
