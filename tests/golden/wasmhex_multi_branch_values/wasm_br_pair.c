// wasmdec: bounds=hidden
__tuple wasm_tuple_7f_7e __usercall wasm_br_pair@<0:r0, 4:r2, 8:r1>()
{
  __tuple wasm_tuple_7f_7e result; // 0:rax.12

  result._r0 = 10;
  HIDWORD(result._r1) = 0;
  LODWORD(result._r1) = 20;
  return result;
}
