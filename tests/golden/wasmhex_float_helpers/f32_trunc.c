// wasmdec: bounds=hidden
float __usercall f32_trunc@<r0>(float a1@<r0>)
{
  return COERCE_FLOAT(truncf(LODWORD(a1)));
}
