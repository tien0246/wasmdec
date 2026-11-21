// wasmdec: bounds=hidden
void __usercall copy(int a1@<r0>, int a2@<r1>)
{
  *(_OWORD *)(unsigned int)a1 = *(_OWORD *)(unsigned int)a2;
}
