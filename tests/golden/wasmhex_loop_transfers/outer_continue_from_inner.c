// wasmdec: bounds=hidden
int __usercall outer_continue_from_inner@<r0>(int a1@<r0>)
{
  unsigned int v2; // kr04_4
  unsigned int i; // kr08_4

  v2 = 0;
LABEL_2:
  while ( v2 < 2 )
  {
    for ( i = 0; i < 2; ++i )
    {
      if ( a1 != 0 )
      {
        a1 = 0;
        v2 = 1;
        goto LABEL_2;
      }
    }
    ++v2;
  }
  return v2;
}
