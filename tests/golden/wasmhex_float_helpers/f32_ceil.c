// wasmdec: bounds=hidden
float __usercall f32_ceil@<r0>(float a1@<r0>)
{
  return COERCE_FLOAT(ceilf(LODWORD(a1)));
}
