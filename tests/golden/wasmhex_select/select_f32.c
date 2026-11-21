// wasmdec: bounds=hidden
// wasmdec ICF-folded with: select_i32 select_i64 select_f64 (+0 more)
float __usercall select_f32@<r0>(int a1@<r0>, float a2@<r1>, float a3@<r2>)
{
  return a1 ? a2 : a3;
}
