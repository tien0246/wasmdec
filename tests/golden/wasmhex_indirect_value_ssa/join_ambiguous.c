// wasmdec: bounds=hidden
int __usercall join_ambiguous@<r0>(int a1@<r0>)
{
  if ( a1 != 0 )
    return target0(a1);
  else
    return target1(0);
}
