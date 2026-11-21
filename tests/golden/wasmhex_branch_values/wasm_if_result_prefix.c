// wasmdec: bounds=hidden
int __usercall wasm_if_result_prefix@<r0>(int a1@<r0>)
{
  int v1; // kr08_4

  if ( a1 != 0 )
    v1 = 10;
  else
    v1 = 20;
  return v1 + 7;
}
