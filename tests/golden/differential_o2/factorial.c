// wasmdec: bounds=hidden
int __usercall factorial@<r0>(int a1@<r0>)
{
  int v1; // kr00_4
  int v2; // kr04_4
  int v3; // kr0C_4
  int v4; // kr10_4

  v1 = a1;
  v2 = 1;
  if ( a1 >= 2 )
  {
    v3 = (a1 - 1) & 7;
    if ( (unsigned int)(a1 - 2) >= 7 )
    {
      v4 = 0;
      v2 = 1;
      do
      {
        v2 *= (a1 + v4 - 7)
            * (a1 + v4 - 6)
            * (a1 + v4 - 5)
            * (a1 + v4 - 4)
            * (a1 + v4 - 3)
            * (a1 + v4 - 2)
            * (a1 + v4 - 1)
            * (a1 + v4);
        v4 -= 8;
      }
      while ( -((a1 - 1) & 0xFFFFFFF8) != v4 );
      v1 = a1 + v4;
    }
    else
    {
      v2 = 1;
    }
    for ( ; v3 != 0; --v3 )
      v2 *= v1--;
  }
  return v2;
}
