// wasmdec: bounds=hidden
int __usercall load_i32@<r0>(int a1@<r0>)
{
  return *(_DWORD *)((unsigned int)a1 + 0x1234LL);
}
