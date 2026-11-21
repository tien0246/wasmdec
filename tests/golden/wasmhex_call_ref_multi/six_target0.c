// wasmdec: bounds=hidden
void __usercall six_target0(int a1@<r0>, void *result@<r1>)
{
  *(_DWORD *)result = a1;
  *((_DWORD *)result + 1) = a1 + 1;
  *((_DWORD *)result + 2) = a1 + 2;
  *((_DWORD *)result + 3) = a1 + 3;
  *((_DWORD *)result + 4) = a1 + 4;
  *((_DWORD *)result + 5) = a1 + 5;
}
