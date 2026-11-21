// wasmdec: bounds=hidden
float __usercall f32_nearest@<r0>(float a1@<r0>)
{
  return COERCE_FLOAT(roundevenf(LODWORD(a1)));
}
