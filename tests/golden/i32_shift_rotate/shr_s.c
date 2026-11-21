// wasmdec: bounds=hidden
int __usercall shr_s@<r0>(int a1@<r0>, int a2@<r1>)
{
  return a1 >> ((unsigned int)a2 & 0x1Fu);
}
