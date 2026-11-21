// wasmdec: bounds=hidden
void __usercall store_end()
{
  unsigned __int8 v0; // kr00_1
  __int128 v1; // kr10_16
  __int64 v2; // kr00_8

  v0 = 0;
  v1 = *MK_FP(1, v0);
  v2 = 65520;
  *MK_FP(1, v2) = v1;
}
