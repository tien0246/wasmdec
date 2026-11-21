// wasmdec: bounds=hidden
int __usercall wasm_loop_backedge@<r0>(int a1@<r0>)
{
  int v2; // kr04_4

  v2 = 0;
  while ( a1 != 0 )
  {
    ++v2;
    --a1;
  }
  return v2;
}
