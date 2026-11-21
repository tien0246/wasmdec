// wasmdec: bounds=hidden
void __usercall read()
{
  helper();
  wasm_memory_init_data1_i32(0, 0, 1u);
}
