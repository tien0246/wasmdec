// wasmdec: bounds=hidden
void __usercall target_b(void *result@<r0>)
{
  qmemcpy(result, "('&%$#\"!87654321HGFEDCBA", 24);
  *((_DWORD *)result + 6) = 1432778632;
}
