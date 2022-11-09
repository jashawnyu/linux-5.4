#include<stdio.h>
#include <unistd.h>

int main (int argc, char *argv[])
{
  for(;;){
    printf("Hello, World!\n");
    sleep(1);
  }
  return 0;
}
