// wasmdec: bounds=hidden
int __usercall load64@<r0>(__int64 a1@<r0:r1>)
{
  return *(_DWORD *)(a1 + 0x100000000LL);
}
