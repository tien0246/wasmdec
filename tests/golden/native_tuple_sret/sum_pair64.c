// wasmdec: bounds=hidden
__int64 __usercall sum_pair64@<r0:r1>()
{
  __int64 r0; // kr00_8
  __int64 r1; // kr08_8
  __int64 v6; // kr00_8
  __int64 result; // eax:ecx
  __tuple wasm_tuple_7e_7e pair64; // [esp+0h] [ebp-10h]

  pair64 = make_pair64();
  r0 = pair64._r0;
  r1 = pair64._r1;
  v6 = r0 + r1;
  HIDWORD(result) = HIDWORD(v6);
  LODWORD(result) = v6;
  return result;
}
