// wasmdec: bounds=hidden
int __usercall grow_memory@<r0>(int a1@<r0>)
{
  return __builtin_wasm_memory_grow(0, a1);
}
