// wasmdec: bounds=hidden
int __usercall externref_typed@<r0>(unsigned int a1@<r0>, unsigned int a2@<r1>, int a3@<r2>)
{
  unsigned int v3; // kr10_4

  v3 = a3 ? a1 : a2;
  return v3 == 0;
}
