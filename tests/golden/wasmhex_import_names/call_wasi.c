// wasmdec: bounds=hidden
int __usercall call_wasi@<r0>(int a1@<r0>)
{
  return wasi_snapshot_preview1_invoke(a1);
}
