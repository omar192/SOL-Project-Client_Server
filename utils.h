#ifndef UTILS_H_
#define UTILS_H_

#include <pthread.h>
#include <errno.h>

#define SYSCALL(r,c,e) \
    if((r=c)==-1) { perror(e);exit(errno); }
#define CHECKNULL(r,c,e) \
    if ((r=c)==NULL) { perror(e);exit(errno); }


//free di multipli argomenti, ultimo argomento == NULL
void frees(void *arg1, ...);


//tokenizza una stringa in base ad un delimitatore e restituisce un array dove ogni entry è puntatore ad ogni token
//controlla inoltre che il numero di token sia quello sperato
char **mystrtok(char* string, const char* delim, int num_token);


/*
  Versione re-implementata della pthread_mutex_lock con controlli
*/
void Pthread_mutex_lock(pthread_mutex_t *mutex);


/*
  Versione re-implementata della pthread_mutex_unlock con controlli
*/
void Pthread_mutex_unlock(pthread_mutex_t *mutex);


#endif /* UTILS_H */