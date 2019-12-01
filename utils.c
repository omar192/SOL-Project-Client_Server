#include <errno.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <stdlib.h>
#include <pthread.h>

#include "utils.h"


//free di multipli argomenti, ultimo argomento == NULL
void frees(void *arg1, ...) {

  #ifdef DEBUG
  printf("frees\n");
  #endif

  va_list args;
  void *vp;
  free(arg1);
  va_start(args, arg1);
  while ((vp = va_arg(args, void *)) != NULL)
    free(vp);
  va_end(args);
}


//tokenizza una stringa in base ad un delimitatore e restituisce un array dove ogni entry è puntatore ad ogni token
//controlla inoltre che il numero di token sia quello sperato
char **mystrtok(char* string, const char* delim, int num_token){

  if(!string || !delim || num_token <= 0) return NULL;

  char **array;
  CHECKNULL(array, calloc(num_token+1, sizeof(char*)), "mystrtok - calloc");


  char* saveptr = NULL;

  int i = 0;
  if ( ( array[i] = strtok_r(string, delim, &saveptr) ) == NULL){
    free(array);
    return NULL;
  }

  for(i = 1; i < num_token-1; i++){

    if ( ( array[i] = strtok_r(NULL, delim, &saveptr) ) == NULL){
      free(array);
      return NULL;
    }

  }

  array[i] =  saveptr;
  
  return array;
}


/*
  Versione re-implementata della pthread_mutex_lock con controlli
*/
void Pthread_mutex_lock(pthread_mutex_t *mutex){
  int err=1;
  if((err=pthread_mutex_lock(mutex))!=0){
    perror("Pthread_mutex_lock:");
    exit(EXIT_FAILURE);
  }
}

/*
  Versione re-implementata della pthread_mutex_unlock con controlli
*/
void Pthread_mutex_unlock(pthread_mutex_t *mutex){
  int err=1;
  if((err=pthread_mutex_unlock(mutex))!=0){
    perror("Pthread_mutex_lock:");
    exit(EXIT_FAILURE);
  }
}


