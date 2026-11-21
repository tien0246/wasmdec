// wasmdec: bounds=hidden
int __usercall f32_copysign_bits@<r0>(int a1@<r0>, int a2@<r1>)
{
  unsigned int v2; // kr0C_4
  unsigned int v3; // kr10_4
  float v4; // kr08_4

  *(float *)&v2 = COERCE_FLOAT((unsigned int)a1);
  *(float *)&v3 = COERCE_FLOAT((unsigned int)a2);
  v4 = COERCE_FLOAT(copysignf(v2, v3));
  return (int)LODWORD(v4);
}
