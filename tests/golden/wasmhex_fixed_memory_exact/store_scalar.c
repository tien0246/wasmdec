// wasmdec: bounds=hidden
void __usercall store_scalar(int a1@<r0>, int a2@<r1>)
{
  *(_DWORD *)(unsigned int)a1 = a2;
}
