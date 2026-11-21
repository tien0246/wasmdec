// wasmdec: bounds=hidden
__tuple wasm_tuple_7e_7e __usercall forward_pair64@<0:r0, 4:r2, 8:r1, 12:r3>()
{
  __int64 r0; // kr00_8
  __int64 r1; // kr08_8
  __tuple wasm_tuple_7e_7e pair64; // [esp+0h] [ebp-10h]
  __tuple wasm_tuple_7e_7e result; // 0:rax.16

  pair64 = make_pair64();
  r0 = pair64._r0;
  r1 = pair64._r1;
  result._r0 = r0;
  result._r1 = r1;
  return result;
}
