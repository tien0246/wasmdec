// wasmdec: bounds=hidden
int __usercall dispatch@<r0>(int a1@<r0>)
{
  int v1; // kr04_4

  switch ( a1 )
  {
    case 0:
    case 1:
    case 2:
    case 3:
    case 4:
    case 5:
    case 6:
    case 7:
      v1 = 11;
      break;
    default:
      v1 = 7;
      break;
  }
  return v1;
}
