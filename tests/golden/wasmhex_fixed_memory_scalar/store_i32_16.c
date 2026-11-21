// wasmdec: bounds=hidden
int __usercall store_i32_16@<r0>(int a1@<r0>, int a2@<r1>)
{
  *(_WORD *)((unsigned int)a1 + 0x100LL) = a2;
  return *(unsigned __int16 *)((unsigned int)a1 + 0x100LL);
}
