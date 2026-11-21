// wasmdec: bounds=hidden
int __usercall wasm_if_result@<r0>(int a1@<r0>)
{
  if ( a1 != 0 )
    return 10;
  else
    return 20;
}
