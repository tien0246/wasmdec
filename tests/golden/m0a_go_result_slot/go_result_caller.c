// wasmdec: bounds=hidden
int __usercall go_result_caller@<r0>(int a1@<r0>)
{
  int v1; // kr04_4
  __int64 v2; // kr08_8

  wasm_global_0 -= 48;
  v1 = wasm_global_0;
  ((void (__usercall *)(__int64@<r0:r1>, __int64@<r2:r3>, __int64@<r6:r7>, __int64, __int64))wasm_func_1)(
    111,
    9,
    0,
    0,
    0);
  v2 = *(_QWORD *)(unsigned int)(v1 + 56);
  wasm_global_0 = v1 + 56;
  return v2;
}
