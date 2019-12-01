#ifndef CLIENT_CONN_H_
#define CLIENT_CONN_H_

//numero tentativi di connessione client
#define MAXTRIES 3

//salvo l'ultimo errore che il server ha inviato
int myerrno;

//estrapola il tipo di errore dalla risposta del server (0 se è OK)
int set_error(char* response);

// NOUSED: versione di perror ma con l'utilizzo di myerrno
void myperror();


int os_connect(char *name);

int os_store(char *name, void *block, size_t len);

void *os_retrieve(char *name);

int os_delete(char *name);

int os_disconnect();

#endif /* CLIENT_CONN_H_ */