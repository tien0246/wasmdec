// wasmdec: bounds=hidden
int __usercall sum_triple32@<r0>()
{
  int r0; // kr00_4
  int r1; // kr04_4
  int r2; // kr08_4
  __tuple wasm_tuple_7f_7f_7f triple32; // [esp+0h] [ebp-10h]

  triple32 = make_triple32();
  r0 = triple32._r0;
  r1 = triple32._r1;
  r2 = triple32._r2;
  return r0 + r1 + r2;
}
