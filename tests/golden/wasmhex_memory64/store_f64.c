// wasmdec: bounds=hidden
void __usercall store_f64(__int64 a1@<r0:r1>, double a2@<r2:r3>)
{
  *(double *)(a1 + 0x100000011LL) = a2;
}
