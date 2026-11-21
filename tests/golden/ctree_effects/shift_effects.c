// wasmdec: bounds=hidden
int __usercall shift_effects@<r0>(int a1@<r0>, int a2@<r1>)
{
  unsigned int v3; // kr08_4
  unsigned int v4; // kr0C_4

  v3 = effect(a1);
  v4 = effect(a2);
  return (int)(v3 << (v4 & 0x1Fu));
}
