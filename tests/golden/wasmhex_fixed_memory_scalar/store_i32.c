// wasmdec: bounds=hidden
int __usercall store_i32@<r0>(int a1@<r0>, int a2@<r1>)
{
  *(_DWORD *)((unsigned int)a1 + 0x100LL) = a2;
  return *(_DWORD *)((unsigned int)a1 + 0x100LL);
}
