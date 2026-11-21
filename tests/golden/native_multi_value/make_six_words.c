// wasmdec: bounds=hidden
void __usercall make_six_words(void *result@<r0>)
{
  *(_DWORD *)result = 17;
  *(_QWORD *)((char *)result + 4) = 34;
  *(_QWORD *)((char *)result + 12) = 51;
  *((_DWORD *)result + 5) = 68;
}
