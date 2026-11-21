// wasmdec: bounds=hidden
int __usercall f32_floor_bits@<r0>(int a1@<r0>)
{
  unsigned int v1; // kr08_4
  float v2; // kr04_4

  *(float *)&v1 = COERCE_FLOAT((unsigned int)a1);
  v2 = COERCE_FLOAT(floorf(v1));
  return (int)LODWORD(v2);
}
