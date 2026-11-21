// wasmdec: bounds=hidden
void __usercall conditional_copy(int a1@<r0>, int a2@<r1>, int a3@<r2>)
{
  if ( a1 != 0 )
    *(_OWORD *)(unsigned int)a2 = *(_OWORD *)(unsigned int)a3;
}
