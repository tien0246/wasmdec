// wasmdec: bounds=hidden
void __usercall wasm_pc_f_loop()
{
  unsigned int v0; // kr10_4

  while ( wasm_global_7 == 0 )
    __indirect_function_table[*(_DWORD *)((unsigned int)(wasm_global_0 - 8) + 2LL)](*(unsigned __int16 *)(unsigned int)(wasm_global_0 - 8));
  wasm_global_7 = 0;
}
