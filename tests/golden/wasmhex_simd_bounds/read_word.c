// wasmdec: bounds=hidden
int __usercall read_word@<r0>(int a1@<r0>)
{
  return *(_DWORD *)(unsigned int)a1;
}
