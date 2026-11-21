// wasmdec: bounds=hidden
// wasmdec ICF-folded with: select_i64 select_f32 select_f64 (+0 more)
int __usercall select_i32@<r0>(int a1@<r0>, int a2@<r1>, int a3@<r2>)
{
  return a1 ? a2 : a3;
}
