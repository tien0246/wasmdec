// wasmdec: bounds=hidden
void __usercall store_f64(int a1@<r0>, double a2@<r1:r2>)
{
  *(double *)((unsigned int)a1 + 0x2003LL) = a2;
}
