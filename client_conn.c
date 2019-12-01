#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <errno.h>

#include "utils.h"
#include "conn.h"
#include "client_conn.h"

static int sockfd = -1;

int set_error(char* response){

	if(!response) return -1;

	if( strncmp(response, "KO ", 3) != 0) return -1;

	char* error_char = strtok(response, " ");
	error_char = strtok(NULL, " ");

	char* saveptr;
	int error_int = strtol(error_char, &saveptr, 10);
	if( saveptr == error_char ) return -1;

	myerrno = error_int;

	return 0;
}


void myperror(){

  switch(myerrno){
    case EINVAL:
      fprintf(stderr, "Argomenti non validi.\n");
      break;
    case ENAMETOOLONG:
      fprintf(stderr, "Nome file o cliente troppo lunghi. Massimo %d caratteri.\n", MAXNAMELEN);
      break;
    case EFBIG:
      fprintf(stderr, "Dimensione file specificata troppo grande. Massimo %d byte.\n", MAXDATASIZE);
      break;
    case ENOTUNIQ:
      fprintf(stderr, "Client già connesso al server.\n");
      break;
     case EEXIST:
     fprintf(stderr, "Client mai registrato.\n");
      break;
    case ENOENT:
      fprintf(stderr, "Nessun file trovato.\n");
      break;
    default:
      perror("Errore libreria esterna");
  }

}

int os_connect(char *name){

	if(!name){
		myerrno = EINVAL;
		return -1;
	}

	int name_len = strlen(name);

	if(  name_len > MAXNAMELEN){
		myerrno = ENAMETOOLONG;
		return -1;
	}

	// File descriptor del nuovo socket
	int connfd;
	struct sockaddr_un serv_addr;

	serv_addr.sun_family=AF_UNIX;
	strncpy(serv_addr.sun_path, SOCKNAME, strlen(SOCKNAME)+1);


	if( (connfd = socket(AF_UNIX, SOCK_STREAM, 0)) == -1){
		myerrno = errno;
		return -1;
	}


	
	if(connect(connfd,(struct sockaddr *) &serv_addr, sizeof(serv_addr))==-1){
		myerrno = errno;
		return -1;
	} else{ //conessione accettata
		
		//preparo messaggio di request
		int size_req = 12 + name_len;
		char *request;
		if ( (request = calloc(size_req, sizeof(char)) ) == NULL) return -1;
		snprintf(request, size_req, "REGISTER %s \n", name);

		//write al server
		if( write(connfd, request, size_req-1) == -1){
			myerrno = errno;
			free(request);
			return -1;
		}

		char *response;
		if ( (response = calloc(MSGERRORLEN, sizeof(char)) ) == NULL) return -1;

		//leggo la risposta
		if( read(connfd, response, MSGERRORLEN) == -1){
			myerrno = errno;
			frees(request, response, NULL);
			return -1;
		}

		if( strncmp(response, "OK \n", 4) == 0) { //ricevuto OK \n
			sockfd = connfd;
			frees(request, response, NULL);
			return 0; 
		}

		//ricevuto un errore
		set_error(response);
		frees(request, response, NULL);

		return -1;	
	}

	return -1;	
	
}

int os_store(char *name, void *block, size_t len){

	if( !name || !block || len <= 0){
		myerrno = EINVAL;
		return -1;
	}

	if (strlen(name) > MAXNAMELEN){
		myerrno = ENAMETOOLONG;
		return -1;
	}

	if (len > MAXDATASIZE) {
		myerrno = EFBIG;
		return -1;
	}

	char* request;

	if ( (request = calloc(MESSAGELEN+len, sizeof(char))) == NULL){
		myerrno = errno;
		return -1;
	}

	snprintf(request, MESSAGELEN, "STORE %s %ld \n ", name, len);

	int len_without_data = strlen(request);

	memcpy( (request+len_without_data), block, len);

	int left = len_without_data+len;
	int r = 0;
	char* bufptr = request;


	while(left>0) {

		if ((r=write(sockfd, request, left)) == -1) {

			if (errno == EINTR) continue;
			myerrno = errno;
			free(request);
			return -1;
		}

		if (r == 0) break;  

		left    -= r;
		bufptr  += r;

	}

	//leggo la risposta
	if( read(sockfd, request, MSGERRORLEN) == -1){
		myerrno = errno;
		free(request);
		return -1;
	}


	if( strncmp(request, "OK \n", 4) == 0){//ricevuto OK \n
		free(request);
		return 0;
	}

	//ricevuto errore
	set_error(request);
	free(request);
	return -1;
}


void *os_retrieve(char *name){

	if(!name){
		myerrno = EINVAL;
		return NULL;
	}

	if(strlen(name) > MAXNAMELEN){
		myerrno = ENAMETOOLONG;
		return NULL;
	}

	//preparo messaggio di request
	int request_len = 12+strlen(name);

	char *request;
	if( (request = calloc(request_len, sizeof(char))) == NULL){
		myerrno = errno;
		return NULL;
	}

	snprintf(request, request_len, "RETRIEVE %s \n", name);

	//write al server
	if( write(sockfd, request, request_len-1) == -1) {
		myerrno = errno;
		free(request);
		return NULL;
	}
	
	free(request);

	//leggo la prima parte della risposta
	char *response;
	if ( (response = calloc(MESSAGELEN, sizeof(char))) == NULL){
		myerrno = errno;
		return NULL;
	}

	if( read(sockfd, response, MESSAGELEN) == -1){
		myerrno = errno;
		free(response);
		return NULL;
	}


	if(strncmp(response, "DATA ", 5) != 0){
		set_error(response);
		free(response);
		return NULL;
	}

	char** tokens_response;
	if( (tokens_response = mystrtok(response, " ", 4)) == NULL){ 
		myerrno = EINVAL;
		free(response);
		return NULL;
	}
	

	char* data_len_char = tokens_response[1];

	char* endptr;
    long data_len_long = strtol(data_len_char, &endptr, 10); //mi salvo la lunghezza
    if( endptr == data_len_char ){
		myerrno = EINVAL;
		frees(response, tokens_response, NULL);
		return NULL;
	}

    if( *tokens_response[2] != '\n' ){
		myerrno = EINVAL;
		frees(response, tokens_response, NULL);
		return NULL;
	}


    int data_space = MESSAGELEN - (8+strlen(data_len_char)); //calcolo quanto spazio è stato lasciato per i dati

    char* data;
	if (  (data = calloc(data_len_long, sizeof(char))) == NULL){  //alloco spazio per caricare tutti dati
		myerrno = errno;
		frees(response, tokens_response, NULL);
		return NULL;
	}

	if( data_len_long < data_space){

		memcpy(data, tokens_response[3], data_len_long);

	} else{

		memcpy(data, tokens_response[3], data_space);

		int left = data_len_long - data_space;

		int r;
		if( (r = read(sockfd, (data+data_space), left)) == -1 ){ 
			myerrno = errno;
			frees(response, tokens_response, NULL);
			return NULL;
		}
	}

	frees(response, tokens_response, NULL);

	return (void *)data;
}

int os_delete(char *name){

	if(!name) {
		myerrno = EINVAL;
		return -1;
	}

	if(strlen(name) > MAXNAMELEN){
		myerrno = ENAMETOOLONG;
		return -1;
	}

	//preparo messaggio di request
	int request_len =10+strlen(name);

	char *request;
	if ( (request = calloc(request_len, sizeof(char))) == NULL){
		myerrno = errno;
		return -1;
	}

	snprintf(request, request_len, "DELETE %s \n", name);

	//write al server
	if( write(sockfd, request, request_len-1) == -1){
		myerrno = errno;
		free(request);
		return -1;
	}

	char *response;
	if ( (response = calloc(MSGERRORLEN, sizeof(char))) == NULL){
		myerrno = errno;
		return -1;
	}


	//leggo la risposta
	if( read(sockfd, response, MSGERRORLEN) == -1){
		myerrno = errno;
		free(request);
		return -1;
	}

	if(strncmp(response, "OK \n", 4) != 0){
		set_error(response);
		frees(request, response, NULL);
		return -1;
	}

	frees(request, response, NULL);
	return 0;
}

int os_disconnect(){

	//preparo messaggio di request
	int size_req = 8;
	char *request;
	if ( (request = calloc(size_req, sizeof(char)) ) == NULL){
		myerrno = errno;
		return -1;
	}

	snprintf(request, size_req, "LEAVE \n");

	//write al server
	if( write(sockfd, request, size_req-1) == -1){
		myerrno = errno;
		free(request);
		return -1;
	}

	char *response;
	if ( (response = calloc(MESSAGELEN, sizeof(char)) ) == NULL){
		myerrno = errno;
		frees(request);
		return -1;
	}

	//leggo la risposta
	if( read(sockfd, response, MSGERRORLEN) == -1){
		myerrno = errno;
		frees(request, response, NULL);
		return -1;
	}

	if(strncmp(response, "OK \n",4) != 0){
		set_error(response);
		frees(request, response, NULL);
		return -1;
	}

	frees(request, response, NULL);
	return 0;

}