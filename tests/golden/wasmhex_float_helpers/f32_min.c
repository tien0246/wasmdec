// wasmdec: bounds=hidden
float __usercall f32_min@<r0>(float a1@<r0>, float a2@<r1>)
{
  return COERCE_FLOAT(fminimumf(LODWORD(a1), LODWORD(a2)));
}
