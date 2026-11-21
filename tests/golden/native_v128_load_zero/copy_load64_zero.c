// wasmdec: bounds=hidden
void __usercall copy_load64_zero(int a1@<r0>, int a2@<r1>)
{
  *(_OWORD *)((unsigned int)a1 + 8LL) = *(unsigned __int64 *)((unsigned int)a2 + 0x18LL);
}
