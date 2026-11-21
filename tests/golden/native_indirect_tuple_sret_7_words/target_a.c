// wasmdec: bounds=hidden
void __usercall target_a(void *result@<r0>)
{
  *(_QWORD *)result = 0x102030405060708LL;
  *((_QWORD *)result + 1) = 0x1112131415161718LL;
  *((_QWORD *)result + 2) = 0x123456789ABCDEFLL;
  *((_DWORD *)result + 6) = 305419896;
}
