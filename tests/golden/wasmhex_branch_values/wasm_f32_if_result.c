// wasmdec: bounds=hidden
float __usercall wasm_f32_if_result@<r0>(int a1@<r0>)
{
  if ( a1 != 0 )
    return 1.5;
  else
    return 2.5;
}
