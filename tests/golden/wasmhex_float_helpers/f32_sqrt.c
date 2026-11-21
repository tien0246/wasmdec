// wasmdec: bounds=hidden
float __usercall f32_sqrt@<r0>(float a1@<r0>)
{
  return COERCE_FLOAT(sqrtf(LODWORD(a1)));
}
