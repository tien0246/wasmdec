// wasmdec: bounds=hidden
__tuple wasm_tuple_7f_7f_7f __usercall forward_triple32@<0:r0, 4:r2, 8:r1>()
{
  int r0; // kr00_4
  int r1; // kr04_4
  int r2; // kr08_4
  __tuple wasm_tuple_7f_7f_7f triple32; // [esp+0h] [ebp-10h]
  __tuple wasm_tuple_7f_7f_7f result; // 0:rax.12

  triple32 = make_triple32();
  r0 = triple32._r0;
  r1 = triple32._r1;
  r2 = triple32._r2;
  result._r0 = r0;
  result._r1 = r1;
  result._r2 = r2;
  return result;
}
