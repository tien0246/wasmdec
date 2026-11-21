// wasmdec: bounds=hidden
int __usercall array_sum@<r0>(int a1@<r0>, int a2@<r1>)
{
  int v3; // kr08_4
  int v4; // kr0C_4
  int v5; // kr10_4
  unsigned int v6; // kr04_4
  unsigned int v7; // kr04_4

  if ( a2 < 1 )
    return 0;
  v3 = a2 & 3;
  if ( (unsigned int)a2 >= 4 )
  {
    v4 = 0;
    v6 = a1;
    v5 = 0;
    do
    {
      v5 += *(_DWORD *)(v6 + 12) + *(_DWORD *)(v6 + 8) + *(_DWORD *)(v6 + 4) + *(_DWORD *)v6;
      v6 += 16;
      v4 += 4;
    }
    while ( (a2 & 0x7FFFFFFC) != v4 );
  }
  else
  {
    v4 = 0;
    v5 = 0;
  }
  if ( (a2 & 3) != 0 )
  {
    v7 = a1 + 4 * v4;
    do
    {
      v5 += *(_DWORD *)v7;
      v7 += 4;
      --v3;
    }
    while ( v3 != 0 );
  }
  return v5;
}
