#include <stdio.h>
#include <syscall.h>

int
main (int argc, char **argv)
{
  int i;
  printf("echo");
    
  for (i = 0; i < argc; i++)
    printf ("%s ", argv[i]);
  printf ("\n");

  return EXIT_SUCCESS;
}
