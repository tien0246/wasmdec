// wasmdec: bounds=hidden
unsigned int __usercall wasm_ref_pair_bridge@<r0>(unsigned int a1@<r0>, unsigned int a2@<r1>)
{
  return select_ref(a1, a2);
}
