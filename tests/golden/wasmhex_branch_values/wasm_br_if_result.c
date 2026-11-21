// wasmdec: bounds=hidden
int __usercall wasm_br_if_result@<r0>(int a1@<r0>)
{
  int v1; // kr04_4

  v1 = 11;
  if ( a1 == 0 )
    return 33;
  return v1;
}
