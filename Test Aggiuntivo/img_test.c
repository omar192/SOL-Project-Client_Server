#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#include "conn.h"
#include "client_conn.h"
#include "utils.h"

int main () {

  int fd;
  struct stat st;
  
  char * file;

  int ntimes = MAXTRIES, i;

  //provo a connettermi
  for(i=0;i<ntimes;i++){
    if (os_connect("Musicista") == -1) {
      sleep(1);
      continue;
    }
    break;
  }

  if( i == ntimes){ //non connesso
    fprintf(stderr, "Errore\n");
    exit(EXIT_FAILURE);
  }


  fd = open("bbking.jpg",O_RDONLY);

  fstat(fd,&st);

  file = (char *) malloc(st.st_size);

  read(fd,file,st.st_size);

  os_store("bbking.jpg", file, st.st_size);

  os_disconnect();

  return 0;

}