#include <stdio.h>
#include<sys/types.h>
#include<sys/stat.h>
#include<fcntl.h>
#include<unistd.h>
#include<sys/mman.h>
#include <string.h>


int main(){
  int fd,fd1;
  char *start;
  char *start1;
  char buf[100];
  fd = open("testfile",O_RDWR);
  fd1 = open("testfile1",O_RDWR);
  start = mmap(NULL,100,PROT_READ|PROT_WRITE,MAP_SHARED,fd,0);
 printf("start=%p\n",start);
 start1 = mmap(NULL,100,PROT_READ|PROT_WRITE,MAP_SHARED,fd1,0);
 printf("start1=%p\n",start1);


 strcpy(buf,start);
 printf("buf = %s\n",buf);

 strcpy(start,"Buf is Not Null!");
  while(1);

 munmap(start,100);
  close(fd);
  close(fd1);

  return 0;

}
