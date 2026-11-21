// wasmdec: bounds=hidden
int __usercall even@<r0>(int a1@<r0>)
{
  if ( a1 != 0 )
    return odd(a1 - 1);
  else
    return 1;
}
