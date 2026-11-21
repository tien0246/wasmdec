// wasmdec: bounds=hidden
int __usercall loop_bound@<r0>(int a1@<r0>)
{
  if ( a1 >= 1 )
    return 2 * a1 + (((a1 - 1) * (a1 - 2)) & 0xFFFFFFFE) - 2;
  else
    return 0;
}
