// wasmdec: bounds=hidden
int __usercall pure_shift@<r0>(int a1@<r0>, int a2@<r1>)
{
  return (int)((unsigned int)a1 << ((unsigned int)a2 & 0x1Fu));
}
