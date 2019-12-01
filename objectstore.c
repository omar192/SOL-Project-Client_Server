#include <unistd.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <pthread.h>
#include <dirent.h>
#include <signal.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <fcntl.h>
#include <sys/un.h>

#include "utils.h"
#include "conn.h"
#include "icl_hash.h"


int quit; //Flag di controllo per la terminazione del server: 0 attivo 1 terminazione



/**
*  @struct sigHandler_t
*  @brief struttura contenente le informazioni da passare al signal handler thread
*
*/
typedef struct {
	sigset_t *set;           /// set dei segnali da gestire (mascherati)
	int signal_pipe;   /// descrittore di scrittura di una pipe senza nome
} sigHandler_t;


/** 
*   Struttura dati che contiene l'hash table dei clients attualmente conessi e altre info.
*
*/
typedef struct {
	icl_hash_t *hash; //       client connessi  
	size_t   qsize;  //        n. max di client
	size_t   qlen;  //         n. di client connessi attualemente
	unsigned int nerrors; //   n. errori totali riscontrati

	pthread_mutex_t  m; //     mutex per la struttura
	pthread_cond_t cworker; // var.cond. su cui si appoggia il server(main) prima di poter chiudere definitivamente
} Stats_t;


Stats_t* stats;



/* ---------------------------------------------------------------------- 
* Hashing funtions
* Well known hash function: Fowler/Noll/Vo - 32 bit version
*/
static inline unsigned int fnv_hash_function( void *key, int len ) {
	unsigned char *p = (unsigned char*)key;
	unsigned int h = 2166136261u;
	int i;
	for ( i = 0; i < len; i++ )
		h = ( h * 16777619 ) ^ p[i];
	return h;
}

// funzione hash per per l'hashing di interi
static inline unsigned int ulong_hash_function( void *key ) {
	int len = sizeof(unsigned long);
	unsigned int hashval = fnv_hash_function( key, len );
	return hashval;
}

// funzione utilizzata dall'hashtable per eliminare le key
void free_key(void* elem){
	free(elem);
}

/* ------------------- Funzioni di utilita' ---------------------------- */

static void LockStats()          { pthread_mutex_lock(&stats->m); }
static void UnlockStats()        { pthread_mutex_unlock(&stats->m); }
static void SignalFinish()     { pthread_cond_signal(&stats->cworker); }
static void WaitToClose()      { pthread_cond_wait(&stats->cworker, &stats->m); }

/* --------------------------------------------------------------------- */


// Inizializza la struttura dedicata alle statistiche
void initializeStats(){

	int notused;

	CHECKNULL(stats, malloc(sizeof(Stats_t)), "initializeStats - malloc");
	memset(stats, 0, sizeof(Stats_t));

	stats->hash = icl_hash_create(MAXCLIENTS, ulong_hash_function, NULL);

	stats->qsize = MAXCLIENTS;
	stats->qlen  = 0;
	stats->nerrors = 0;

	SYSCALL(notused, pthread_mutex_init(&stats->m,NULL), "initializeStats - mutex_init");
	SYSCALL(notused, pthread_cond_init(&stats->cworker,NULL), "initializeStats - cond_init");

}



//Funzione chiamata prima di uscire
//Dealloca il necessario
void cleanup(){

	//elimino eventuale file di socket
	unlink(SOCKNAME);
	//ripulisco struttura dati Stats_t
	if (stats->hash) { icl_hash_destroy(stats->hash, free_key, NULL); }
	if (&stats->m) pthread_mutex_destroy(&stats->m);
	if (&stats->cworker) pthread_cond_destroy(&stats->cworker);
	free(stats);

}



//Funzione che manda un messaggio di errore ad un client
int erroccur(long connfd, int error){

	if(connfd < 0) return -1;

	LockStats();
	stats->nerrors++;
	UnlockStats();

	char* msg;
	CHECKNULL(msg, calloc(MSGERRORLEN, sizeof(char)), "calloc (erroccur)");

	snprintf(msg, MSGERRORLEN, "KO %d \n", error);

	if( write(connfd, msg, strlen(msg)) == -1){
		free(msg);
		return -1;
	}

	free(msg);

	return 0;
}


//Dato un puntatore ad un header, lo converte in intero
op_t getop(char *header){


	if(!header) return -1;

	char *operations[NOPERATIONS] = 
	{ "REGISTER",
	"STORE",
	"RETRIEVE",
	"DELETE",
	"LEAVE"
	};


	for(int i=0; i< NOPERATIONS; i++){
		if(strcmp(operations[i], header) == 0){
		return i;
		}
	}

	return -1;

}



/***********************************************************************************/
/*********** Funzioni per gestire le varie richieste da parte del client ***********/
/***********************************************************************************/

//REGISTER 
char* register_fnct(char* remaining, long connfd){

	char** request;
	if( (request = mystrtok(remaining, " ", 2) ) == NULL){
		erroccur(connfd, EINVAL);
		return NULL;
	}

	int notused;

	char* name = request[0];
	int name_len = strlen(name);
	if( name_len > MAXNAMELEN){
		erroccur(connfd, ENAMETOOLONG);
		return NULL;
	}

	if( *request[1] != '\n') {
		erroccur(connfd, EINVAL);
		free(request);
		return NULL;
	}

	//se è andato tutto bene

	char* path;
	CHECKNULL(path, calloc(strlen(DATAPATH)+1+name_len+1, sizeof(char)), "register_fnct - calloc");
	snprintf(path, strlen(DATAPATH)+1+name_len+1, "%s/%s", DATAPATH, name); //si definisce il path


	if( mkdir(path, S_IRWXU) == -1){ //S_IRWXU sta per "read, write, execute/search by owner"
		if(errno != EEXIST){ //se esiste già una cartella col nome del client ci va bene
			frees(path, request, NULL);
			erroccur(connfd, errno);
			return NULL;
		}
	} 

	char* client_name;
	CHECKNULL(client_name, malloc(sizeof(char)*(name_len+1)), "malloc (register_fnct)");
	snprintf(client_name, name_len+1, "%s", name);

	//si aggiunge alla lista dei client connessi
	LockStats();
	if( !icl_hash_insert(stats->hash, client_name, (void*)connfd)){   //il nome passato è già presente tra quelli connessi
		UnlockStats();
		frees(path, request, NULL);
		erroccur(connfd, ENOTUNIQ);
		return NULL;
	} else{
		stats->qlen++;
		UnlockStats();


		SYSCALL(notused, write(connfd, "OK \n", 4), "write");

		frees(path, request, NULL);

		return client_name;
	}
}


//STORE
int store_fnct(char* client_name, char* remaining, long connfd){

	int r, w, notused;


	char** request;
	if( (request = mystrtok(remaining, " ", 4) ) == NULL) {
		erroccur(connfd, EINVAL);
		return -1;
	}

	char* file_name = request[0];
	int file_name_len = strlen(file_name);
	if( file_name_len > MAXNAMELEN){
		erroccur(connfd, ENAMETOOLONG);
		free(request);
		return -1;
	}

	char* file_size_str = request[1];
	if( strlen(file_size_str) > 10) {
		free(request);
		erroccur(connfd, EFBIG);
		return -1;
	}

	char* endptr;
	long file_size_long = strtol(file_size_str, &endptr, 10);

	if( endptr == file_size_str) {
		erroccur(connfd, EINVAL);
		free(request);
		return -1;
	}

	if( file_size_long > MAXDATASIZE) {
		erroccur(connfd, EFBIG);
		free(request);
		return -1;
	}



	if( *request[2] != '\n' ) {
		erroccur(connfd, EINVAL);
		free(request);
		return -1;
	}

	int data_space = MESSAGELEN - (10+file_name_len+strlen(file_size_str)); //calcolo quanto spazio è stato lasciato per i dati
	char* data;
	CHECKNULL(data, calloc(file_size_long, sizeof(char)), "store_fnct - calloc"); //alloco spazio per caricare tutti dati

	int left;
	char* bufptr;

	if( file_size_long < data_space){

		memcpy(data, request[3], file_size_long);

	} else{

		memcpy(data, request[3], data_space);

		left = file_size_long - data_space;
		bufptr = data+data_space;

		while(left>0) {

			if ((r = read(connfd, bufptr, left)) == -1) {

				if (errno == EINTR) continue;

				frees(request, data, NULL);
				return -1;
			}

			if (r == 0) break;  

			left    -= r;
			bufptr  += r;

		}

	}

	int fd_file;
	char* path = malloc(sizeof(char)*(3+strlen(DATAPATH)+strlen(client_name)+file_name_len));
	snprintf(path, 3+strlen(DATAPATH)+strlen(client_name)+file_name_len, "%s/%s/%s", DATAPATH, client_name, file_name);


	// Apro il nuovo file per scriverci, se il file non esiste lo creo.
	if ( (fd_file = open(path, O_WRONLY|O_CREAT, 0777)) == -1){
		erroccur(connfd, errno);
		frees(request, path, NULL);
		return -1;
	}

	left = file_size_long;
	bufptr = data;

	while(left>0) {

		if ((w = write(fd_file, bufptr, left)) == -1) {

			if (errno == EINTR) continue;

			frees(request, data, path, NULL);
			close(fd_file);
			return -1;
		}

		if (w == 0) break;  

		left    -= w;
		bufptr  += w;

	}

	frees(request, data, path, NULL);
	close(fd_file);

	SYSCALL(notused, write(connfd, "OK \n", 4), "write");

	return 0;
}


//RETRIEVE
int retrieve_fnct(char* client_name, char* remaining, long connfd){

	int notused, r;

	char** request;
	if( (request = mystrtok(remaining, " ", 2) ) == NULL) {
		erroccur(connfd, EINVAL);
		return -1;
	}

	char* file_name = request[0];
	int file_name_len = strlen(file_name);
	if( file_name_len > MAXNAMELEN) {
		erroccur(connfd, ENAMETOOLONG);
		free(request);
		return -1;
	}

	if( *request[1] != '\n' ) {
		erroccur(connfd, EINVAL);
		return -1;
	}


	int fd_file;
	char* path = malloc(sizeof(char)*(strlen(DATAPATH)+strlen(client_name)+strlen(file_name)+3));
	snprintf(path, strlen(DATAPATH)+strlen(client_name)+strlen(file_name)+3, "%s/%s/%s", DATAPATH, client_name, file_name);

	// Apro il file
	if( (fd_file = open(path, O_RDONLY)) == -1) {
		erroccur(connfd, errno);
		frees(request, path, NULL);
		return -1;
	}

	struct stat statbuf;

	if( (notused = stat(path, &statbuf)) == -1 ) {
		erroccur(connfd, errno);
		close(fd_file);
		frees(request, path, NULL);
		return -1;
	}

	long file_size = statbuf.st_size;


	char* response;
	CHECKNULL(response, calloc(MESSAGELEN+file_size, sizeof(char)), "calloc (os_store)");

	snprintf(response, MESSAGELEN, "DATA %ld \n ", file_size);

	int len_without_data = strlen(response);
	int left = file_size;
	char* bufptr = response+len_without_data;


	while(left>0) {

		if ((r = read(fd_file, bufptr, left)) == -1) {

			if (errno == EINTR) continue;

			close(fd_file);
			frees(request, path, response, NULL);
			return -1;
		}

		if (r == 0) break;  

		left    -= r;
		bufptr  += r;

	}


	left = len_without_data+file_size;
	bufptr = response;

	while(left>0) {

		if ((r = write(connfd, bufptr, left)) == -1) {

			if (errno == EINTR) continue;

			close(fd_file);
			frees(request, path, response, NULL);
			return -1;
		}

		if (r == 0) break;  

		left    -= r;
		bufptr  += r;

	}

	close(fd_file);
	frees(request, path, response, NULL);
	return 0;  
}

//DELETE
int delete_fnct(char* client_name, char* remaining, long connfd){

	int notused;

	char** request;
	if( (request = mystrtok(remaining, " ", 2) ) == NULL) {
		erroccur(connfd, EINVAL);
		return -1;
	}

	char* file_name = request[0];
	int file_name_len = strlen(file_name);
	if( file_name_len > MAXNAMELEN) {
		erroccur(connfd, ENAMETOOLONG);
		return -1;
	}

	if( *request[1] != '\n' ) {
		free(request);
		erroccur(connfd, EINVAL);
		return -1;
	}


	char* path = malloc(sizeof(char)*(strlen(DATAPATH)+strlen(client_name)+strlen(file_name)+3));
	snprintf(path, strlen(DATAPATH)+strlen(client_name)+strlen(file_name)+3, "%s/%s/%s", DATAPATH, client_name, file_name);

	// Apro il nuovo file creato per scriverci, se il file non esiste lo creo.
	if( remove(path) == -1) {
		erroccur(connfd, errno);
		frees(request, path, NULL);
		return -1;
	}

	SYSCALL(notused, write(connfd, "OK \n", 4), "write");

	frees(request, path, NULL);
	return 0;
}

/************************************************************************************/




/*********************************************************************************************/
/************************************** Funzione THREAD **************************************/
/*********************************************************************************************/

void *threadF(void *arg) {

	//maschero tutti i segnali per farli poi gestire dal sigHandler
	sigset_t mask;
	sigemptyset(&mask);
	sigaddset(&mask, SIGINT); 
	sigaddset(&mask, SIGQUIT);
	sigaddset(&mask, SIGTERM);
	sigaddset(&mask, SIGUSR1);
	sigaddset(&mask, SIGPIPE);

	if (pthread_sigmask(SIG_BLOCK, &mask,NULL) != 0) {
		fprintf(stderr, "FATAL ERROR\n");
		exit(EXIT_FAILURE);
	}

	long connfd = (long)arg;
	int leave = 0; //Flag per quando dobbiamo chiudere il thread
	char *client_name = NULL; //nome del client che comunica col thread
	op_t header;
	char* char_header;
	char* remaining;
	int r = 0;

	//inizializzo buffer per la richiesta
	char* requestbuf;
	CHECKNULL(requestbuf, calloc(MESSAGELEN, sizeof(char)), "threafF - calloc"); 


	// inizializzo select per evitare che il thread si blocchi su una read e quindi per evitare che, in caso di ricezione
	// di una segnale di arresto, non si debba aspettare che il client effettui una write per poter chiudere l'intero server
	fd_set set, tmpset;
	FD_ZERO(&set);
	FD_ZERO(&tmpset);

	FD_SET(connfd, &set); // aggiungo il fd della conessione al master set

	//inizializzo timeout select
	struct timeval timeout = {SECONDS, MSECONDS};

	//ciclo infinito
	while(!quit && !leave){

		// copio il set nella variabile temporanea per la select
		tmpset = set;

		//mi metto in attesa che qualche fd sia pronto (in particolare connfd)
		if (select(connfd+1, &tmpset, NULL, NULL, &timeout) == -1) {
			erroccur(connfd, errno);
			exit(EXIT_FAILURE);
		}

		if (!FD_ISSET(connfd, &tmpset)){ //non era connfd
			timeout.tv_sec = SECONDS;
			timeout.tv_usec = MSECONDS;
			continue;
		}

		//Se lo era, leggo la richiesta dal client
		if( (r = read(connfd, requestbuf, MESSAGELEN)) == -1){
			erroccur(connfd, errno);
			leave = 1;
			break;
		}

		if( quit == 1){
			erroccur(connfd, EPIPE);
			leave = 1;
			break;
		}


		if ( ( char_header = strtok_r(requestbuf, " ", &remaining) ) == NULL){
			if ( erroccur(connfd, errno) == -1){
				leave = 1;
				break;
			}
			continue;
		}

		if( (header = getop(char_header)) == -1){
			if ( erroccur(connfd, EINVAL) == -1){
				leave = 1;
				break;
			}
			continue;
		}


		//switch richieste
		switch (header) {

			case 0: //REGISTER
				if( !client_name) { //non è connesso
					if( (client_name = register_fnct(remaining, connfd)) == NULL) {
					leave = 1;
					}
				} else{ //è già connesso
					erroccur(connfd, ENOTUNIQ);
					leave = 1;
				} 
				break;

			case 1: //STORE
				if(client_name != NULL){ //è connesso
					store_fnct(client_name, remaining, connfd);
				} else{ //Non ha mai effettuato la REGISTER
					erroccur(connfd, ENOTUNIQ);
					leave = 1;
				}
				break;

			case 2: //RETRIEVE
				if(client_name != NULL){ //è connesso
					retrieve_fnct(client_name, remaining, connfd);
				} else{ //Non ha mai effettuato la REGISTER
					erroccur(connfd, ENOTUNIQ);
					leave = 1;
				}
				break;

			case 3: //DELETE
				if(client_name != NULL){ //è connesso
					delete_fnct(client_name, remaining, connfd);
				} else{ //Non ha mai effettuato la REGISTER
					erroccur(connfd, ENOTUNIQ);
					leave = 1;
				}
				break;

			case 4: //LEAVE
				if(client_name != NULL){ //è connesso
					LockStats();
					icl_hash_delete(stats->hash, client_name, free_key, NULL);
					stats->qlen--;
					UnlockStats();
					write(connfd, "OK \n", 4);
					leave = 1;
					client_name = NULL;
				} else{ // Non ha mai effettuato la REGISTER
					erroccur(connfd, ENOTUNIQ);
					leave = 1;
				}
				break;

			default:
				break;
		}

		memset(requestbuf, 0, MESSAGELEN);

	}//fine while


	if(client_name != NULL){
		LockStats();
		icl_hash_delete(stats->hash, client_name, free_key, NULL);
		stats->qlen--;
		UnlockStats();
		client_name = NULL;
	}


	free(requestbuf);
	close(connfd);
	SignalFinish(stats);


	pthread_exit(0);
}

/*********************************************************************************************/




/**********************************************************************************/
/************ Funzioni utilizzate in caso si riceva un segnale SIGUSR1 ************/
/**********************************************************************************/


//Funzione ausiliaria per lsR
int isdot(const char dir[]) {
	int l = strlen(dir);

	if ( (l>0 && dir[l-1] == '.') ) return 1;

	return 0;
}

//Funzione chiamata ad ogni SIGUSR1: esegue un resoconto della cartella dei dati e stampa su stdout
void lsR(const char nomedir[]) {

	// controllo che il parametro sia una directory
	struct stat statbuf;

	if ( stat(nomedir,&statbuf) == -1){
		perror("stat");
		return;
	}

	DIR * dir;

	//Apro DATAPATH ./data
	if ((dir=opendir(nomedir)) == NULL) {
		perror("opendir");
		return;
	}

	struct dirent *file1;


	int num_files_tmp = 0;
	int num_files_tot = 0;
	int num_clients = 0;

	int size_tmp = 0;
	int size_tot = 0;



	printf("\n\033[30;48;5;15m%10s%15s%13s%16s   \033[0m\n","CLIENT", "CONNESSO", "N.FILES", "DIMENSIONE");

	//Leggo tutte i file in DATAPATH (in particolare le cartelle, ovveri i client registrati)
	while((errno=0, file1 =readdir(dir)) != NULL) {


		struct stat statbuf1;
		char dirname[MAXNAMELEN];

		strncpy(dirname,nomedir,strlen(nomedir)+1);
		strncat(dirname,"/", 2);
		strncat(dirname,file1->d_name, strlen(file1->d_name)+1);

		if (stat(dirname, &statbuf1)==-1) {
			perror("eseguendo la stat");
			return;
		}

		//Cartella di un client trovata
		if(S_ISDIR(statbuf1.st_mode)) {

			if ( !isdot(dirname) ) { //se non è la cartella stessa o quella precedente

				printf("%10s", file1->d_name);

				DIR * dir_interna;

				if ((dir_interna=opendir(dirname)) == NULL) {//la apro
					perror("opendir");
					return;
				}

				struct dirent *file2;

				while((errno=0, file2 =readdir(dir_interna)) != NULL) {//comincio a leggere tutti i file

					struct stat statbuf2;
					char filename[MAXNAMELEN];

					strncpy(filename,nomedir,strlen(nomedir)+1);
					strncat(filename,"/", 2);
					strncpy(filename,dirname,strlen(dirname)+1);
					strncat(filename,"/", 2);
					strncat(filename,file2->d_name, strlen(file2->d_name)+1);

					if (stat(filename, &statbuf2)==-1) {
						perror("eseguendo la stat");
						return;
					}

					if(S_ISREG(statbuf2.st_mode)) {
						num_files_tmp++;
						size_tmp += statbuf2.st_size;
					}

				}

				closedir(dir_interna);

				char client_to_found[MAXNAMELEN];

				strncpy(client_to_found, file1->d_name, strlen(file1->d_name)+1);

				LockStats();
				if( icl_hash_find(stats->hash, (void*) file1->d_name) != NULL ){
					printf("%12s", "SI");
				} else{
					printf("%12s", "NO");
				} 
				UnlockStats();


				printf("%13d", num_files_tmp);
				printf("%17d\n", size_tmp);

				num_files_tot += num_files_tmp;
				size_tot += size_tmp;
				num_clients++;
			} 
		}

		size_tmp = 0;
		num_files_tmp = 0;

	}


	closedir(dir);

	printf("\nClient registrati: %d\n", num_clients);
	printf("Dimensione totale: %d bytes\n", size_tot);
	printf("File totali: %d\n\n", num_files_tot);

	return;
}

/***********************************************/
/************ Signal Handler Thread ************/
/***********************************************/

static void *sigHandler(void *arg) {

	int leave = 0;
	sigset_t *set = ((sigHandler_t*)arg)->set;
	int fd_pipe   = ((sigHandler_t*)arg)->signal_pipe;

	while(!quit && !leave) {

		int sig;
		int r = sigwait(set, &sig);

		if (r != 0) {
			errno = r;
			perror("FATAL ERROR 'sigwait'");
			pthread_exit(&r);
		}

		write(fd_pipe, &sig, sizeof(int));

		if(sig == SIGINT || sig == SIGTERM || sig == SIGQUIT) leave = 1;
	}

	pthread_exit(0);
}

/**********************************************************************************/



//funzione incaricata di spawnare un nuovo thread passandoli il file descriptor con cui dialogare col nuovo client
void spawn_thread(long connfd) {
	pthread_attr_t thattr;
	pthread_t thid;

	if (pthread_attr_init(&thattr) != 0) {
		fprintf(stderr, "pthread_attr_init FALLITA\n");
		close(connfd);
		return;
	}
	// settiamo il thread in modalità detached
	if (pthread_attr_setdetachstate(&thattr,PTHREAD_CREATE_DETACHED) != 0) {
		fprintf(stderr, "pthread_attr_setdetachstate FALLITA\n");
		pthread_attr_destroy(&thattr);
		close(connfd);
		return;
	}

	if (pthread_create(&thid, &thattr, threadF, (void*)connfd) != 0) {
		fprintf(stderr, "pthread_create FALLITA");
		pthread_attr_destroy(&thattr);
		close(connfd);
		return;
	}
}


/******************************************************************************/
/************************************ MAIN ************************************/
/******************************************************************************/

int main(int argc, char * argv[]){


	unlink(SOCKNAME);
	atexit(cleanup);


	/*
	*********************************************
	GESTIONE DEI SEGNALI
	*********************************************
	*/

	//maschero tutti i segnali per farli poi gestire al sigHandler
	sigset_t mask;
	sigemptyset(&mask);
	sigaddset(&mask, SIGINT); 
	sigaddset(&mask, SIGQUIT);
	sigaddset(&mask, SIGTERM);
	sigaddset(&mask, SIGUSR1);

	if (pthread_sigmask(SIG_BLOCK, &mask,NULL) != 0) {
		fprintf(stderr, "FATAL ERROR\n");
		exit(EXIT_FAILURE);
	}

	// ignoro SIGPIPE per evitare di essere terminato da una scrittura su un socket
	struct sigaction s;
	memset(&s,0,sizeof(s));    
	s.sa_handler=SIG_IGN;
	if ( (sigaction(SIGPIPE,&s,NULL) ) == -1 ) {   
		perror("sigaction");
		exit(EXIT_FAILURE);
	}

	/*
	* La pipe viene utilizzata come canale di comunicazione tra il signal handler thread ed il 
	* thread listener per notificare la terminazione o SIGUSR1.
	*
	*/
	int signal_pipe[2];
	if (pipe(signal_pipe)==-1) {
		perror("pipe");
		exit(EXIT_FAILURE);
	}





	//creo thread per gestire i segnali
	pthread_t sighandler_thread;


	sigHandler_t handlerArgs = { &mask, signal_pipe[1] };

	if (pthread_create(&sighandler_thread, NULL, sigHandler, &handlerArgs) != 0) {
		fprintf(stderr, "errore nella creazione del signal handler thread\n");
		exit(EXIT_FAILURE);
	}

	//funzione di inizializzazione per la struttura dati stats
	initializeStats();


	//inizializzo socket
	int listenfd;
	if ((listenfd= socket(AF_UNIX, SOCK_STREAM, 0)) == -1) {
		perror("socket");
		exit(EXIT_FAILURE);
	}

	struct sockaddr_un serv_addr;
	memset(&serv_addr, '0', sizeof(serv_addr));
	serv_addr.sun_family = AF_UNIX;    
	strncpy(serv_addr.sun_path, SOCKNAME, strlen(SOCKNAME)+1);

	//collego fd della socket col mio indirizzo
	if (bind(listenfd, (struct sockaddr*)&serv_addr,sizeof(serv_addr)) == -1) {
		perror("bind");
		exit(EXIT_FAILURE);
	}

	//server printo all'ascolto
	if (listen(listenfd, MAXBACKLOG) == -1) {
		perror("listen");
		exit(EXIT_FAILURE);
	}

	//inizializzo la select per capire se sto ricevendo 
	fd_set set, tmpset;
	FD_ZERO(&set);
	FD_ZERO(&tmpset);

	FD_SET(listenfd, &set);        // aggiungo il listener fd al master set
	FD_SET(signal_pipe[0], &set);  // aggiungo il descrittore di lettura della signal_pipe

	// tengo traccia del file descriptor con id piu' grande
	int fdmax = (listenfd > signal_pipe[0]) ? listenfd : signal_pipe[0];

	//inizializzo timeout select
	struct timeval timeout = {SECONDS, MSECONDS};


	long connfd;
	int i = 0;

	quit = 0;

	//ciclo infinito server
	while(!quit) {

		// copio il set nella variabile temporanea per la select
		tmpset = set;

		//mi metto in attesa che qualche fd sia pronto
		if (select(fdmax+1, &tmpset, NULL, NULL, &timeout) == -1) {
			perror("select");
			exit(EXIT_FAILURE);
		}

		// cerco di capire da quale fd abbiamo ricevuto una richiesta
		for(i=0;!quit && i <= fdmax; i++) {
			if (FD_ISSET(i, &tmpset)) {

				//ho ricevuto un segnale e l'handler thread me lo ha comunicato sulla pipe
				if (i == signal_pipe[0]) {
					int sig;
					read(signal_pipe[0], &sig, sizeof(int));

					switch(sig) {

						case SIGINT:
						case SIGTERM:
						case SIGQUIT:
							quit = 1;  //setto variabile quit in modo da far terminare il tutto
							pthread_join(sighandler_thread, NULL);
							break;

						case SIGUSR1:
							lsR(DATAPATH);
							break;

						default:  
							break; 
					}

					break;
				}


				if (i == listenfd && !quit && (stats->qlen != stats->qsize)) { // e' una nuova richiesta di connessione

					SYSCALL(connfd, accept(listenfd, (struct sockaddr*)NULL ,NULL), "accept");
					spawn_thread(connfd);

				}
			}
		}//for

		timeout.tv_sec = SECONDS;
		timeout.tv_usec = MSECONDS;

	}//while

	//Controllo che stats->qlen == 0
	LockStats();
	while(stats->qlen != 0){ WaitToClose(stats); }
	UnlockStats();


	return 0;

}//main
