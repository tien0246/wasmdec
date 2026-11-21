// wasmdec: bounds=hidden
void __usercall copy32(int a1@<r0>, int a2@<r1>, int a3@<r2>)
{
  memmove((unsigned int)a1, (unsigned int)a2, a3);
}
