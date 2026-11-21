// wasmdec: bounds=hidden
int __usercall wasm_func_2@<r0>(int a1@<r0>)
{
  wasm_global_0 -= 32;
  wasm_global_0 += 32;
  return 2 * a1;
}
