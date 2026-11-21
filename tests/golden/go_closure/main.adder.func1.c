// wasmdec: bounds=hidden
int __usercall main.adder.func1@<r0>(int a1@<r0>)
{
  int v1; // kr04_4

  v1 = go_sp;
  *(_DWORD *)(unsigned int)(go_sp + 16) = *(_DWORD *)&byte_2[go_sp + 6] + *(_DWORD *)&byte_2[(unsigned int)go_ctxt + 6];
  go_sp = v1 + 8;
  return 0;
}
