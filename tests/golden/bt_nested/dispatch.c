// wasmdec: bounds=hidden
int __usercall dispatch@<r0>(int a1@<r0>)
{
  int v1; // kr00_4
  int v2; // kr04_4

  v1 = a1;
  v2 = 0;
  if ( a1 >= 100 )
    v1 = a1 - 100;
  switch ( v1 )
  {
    case 0:
      v2 = 10;
      break;
    case 1:
      v2 = 20;
      break;
    case 2:
      v2 = 30;
      break;
    default:
      return v2;
  }
  return v2;
}
