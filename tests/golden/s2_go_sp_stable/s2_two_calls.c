// wasmdec: bounds=hidden
int __usercall s2_two_calls@<r0>(int a1@<r0>)
{
  ((void (__usercall *)())wasm_func_3)();
  ((void (__usercall *)())wasm_func_3)();
  return 5;
}
