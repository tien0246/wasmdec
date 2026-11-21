// wasmdec: bounds=hidden
int __usercall memory_pages@<r0>()
{
  return __builtin_wasm_memory_size(0);
}
