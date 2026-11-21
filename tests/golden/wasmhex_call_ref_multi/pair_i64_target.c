// wasmdec: bounds=hidden
__tuple wasm_tuple_7f_7e __usercall pair_i64_target@<0:r0, 4:r2, 8:r1>(int a1@<r0>)
{
  __tuple wasm_tuple_7f_7e result; // 0:rax.12

  HIDWORD(result._r1) = a1 >> 31;
  LODWORD(result._r1) = a1;
  result._r0 = a1;
  return result;
}
