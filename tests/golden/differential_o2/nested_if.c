// wasmdec: bounds=hidden
int __usercall nested_if@<r0>(int a1@<r0>, int a2@<r1>, int a3@<r2>)
{
  if ( a1 < 1 )
    return 4;
  if ( a2 < 1 )
    return 3;
  return a3 < 1 ? 2u : 1u;
}
