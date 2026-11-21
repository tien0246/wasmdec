// wasmdec: bounds=hidden
__tuple wasm_tuple_7d_7f_7c __usercall wasm_call_mixed@<0:r0, 4:r2, 8:r1, 12:r3>()
{
  float r0; // kr00_4
  int r1; // kr04_4
  double r2; // kr08_8
  __tuple wasm_tuple_7d_7f_7c v8; // [esp+0h] [ebp-10h]
  __tuple wasm_tuple_7d_7f_7c result; // 0:rax.16

  v8 = wasm_func_6();
  r0 = v8._r0;
  r1 = v8._r1;
  r2 = v8._r2;
  result._r0 = r0;
  result._r1 = r1;
  result._r2 = r2;
  return result;
}
