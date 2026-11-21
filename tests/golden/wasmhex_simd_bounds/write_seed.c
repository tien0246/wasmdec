// wasmdec: bounds=hidden
void __usercall write_seed()
{
  unsigned __int8 v0; // kr00_1

  v0 = 0;
  *MK_FP(1, v0) = 305419896;
}
