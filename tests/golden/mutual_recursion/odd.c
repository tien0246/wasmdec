// wasmdec: bounds=hidden
int __usercall odd@<r0>(int a1@<r0>)
{
  if ( a1 != 0 )
    return even(a1 - 1);
  else
    return 0;
}
