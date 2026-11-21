// wasmdec: bounds=hidden
float __usercall f32_floor@<r0>(float a1@<r0>)
{
  return COERCE_FLOAT(floorf(LODWORD(a1)));
}
