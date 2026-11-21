// wasmdec: bounds=hidden
int __usercall pointer_arith@<r0>(int a1@<r0>, int a2@<r1>)
{
  return *(_DWORD *)((unsigned int)(a1 + 4 * a2) + 4LL) + *(_DWORD *)(unsigned int)(a1 + 4 * a2);
}
