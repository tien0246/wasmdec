// wasmdec: bounds=hidden
int __usercall dispatch@<r0>(int a1@<r0>)
{
  int v1; // kr04_4

  v1 = a1 + 100;
  switch ( a1 )
  {
    case 0:
      v1 = a1 + 1100;
    case 1:
      v1 += 2000;
    case 2:
      v1 += 3000;
      break;
    default:
      return v1;
  }
  return v1;
}
