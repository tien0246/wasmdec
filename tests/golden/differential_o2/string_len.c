// wasmdec: bounds=hidden
int __usercall string_len@<r0>(int a1@<r0>)
{
  int v1; // kr04_4
  unsigned int v2; // kr08_4
  int v3; // kr0C_4

  v1 = 0;
  do
  {
    v2 = a1 + v1;
    v3 = ++v1;
  }
  while ( *(_BYTE *)v2 != 0 );
  return v3 - 1;
}
