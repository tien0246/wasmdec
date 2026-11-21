// wasmdec: bounds=hidden
int __usercall load_max_offset@<r0>(int a1@<r0>)
{
  return *(_DWORD *)((unsigned int)a1 + 0xFFFFFFFFLL);
}
