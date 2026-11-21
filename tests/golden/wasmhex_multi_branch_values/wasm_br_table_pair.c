// wasmdec: bounds=hidden
__tuple wasm_tuple_7f_7e __usercall wasm_br_table_pair@<0:r0, 4:r2, 8:r1>(int a1@<r0>)
{
  int v1; // kr10_4
  __int64 v2; // kr18_8
  __tuple wasm_tuple_7f_7e result; // 0:rax.12

  if ( a1 == 0 )
  {
    v1 = 11;
    v2 = 21;
  }
  result._r0 = v1;
  HIDWORD(result._r1) = HIDWORD(v2);
  LODWORD(result._r1) = v2;
  return result;
}
