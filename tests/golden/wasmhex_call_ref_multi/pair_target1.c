// wasmdec: bounds=hidden
__tuple wasm_tuple_7f_7f __usercall pair_target1@<r2:r0>(int a1@<r0>)
{
  int v1; // kr08_4
  __tuple wasm_tuple_7f_7f result; // rax

  v1 = 3 * a1;
  result._r0 = 2 * a1;
  result._r1 = v1;
  return result;
}
