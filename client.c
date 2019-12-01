#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <signal.h>

#include "conn.h"
#include "client_conn.h"
#include "utils.h"



/******** Struttura statistiche client ********/

typedef struct{
	char client_name[MAXNAMELEN];	// nome client

	int n_op_register;	//	n. operazioni di REGISTER
	int n_op_store;		//	n. operazioni di STORE
	int n_op_retrieve;	// 	n. operazioni di RETRIEVE
	int n_op_delete;		//	n. operazioni di DELETE
	int n_op; 	//	n. operazioni totali effettuate
	int n_success; 	//	n. operazioni totali effettuate con successo
	int n_failed; 		//	n. operazioni totali fallite

	// char** log;
} ClientStat_t;


ClientStat_t* stats;

void initializeStats(ClientStat_t* stats){

	stats->n_op_register = 0;
	stats->n_op_store = 0;
	stats->n_op_retrieve = 0;
	stats->n_op_delete = 0;
	stats->n_op = 0;
	stats->n_success = 0;
	stats->n_failed = 0;
	return;
}


void print_log(char* type_op, int result){

	stats->n_op++;

	time_t rawtime;
	struct tm *info;
	
	time( &rawtime );

	info = localtime( &rawtime );

	char date[20];

	strftime(date,20,"[%x %X]", info);

	if(result == 0){
		stats->n_success++;
		printf("%s %s %s success.\n", stats->client_name, date, type_op);
	} else{
		stats->n_failed++;
		const char *errstring;
		errstring = strerror (myerrno);
		printf("%s %s %s error: %d. %s\n", stats->client_name, date, type_op, myerrno, errstring);
	}

}
	


void batteria_store1(){

	int* block;

	char* name_file; 

	CHECKNULL(name_file, calloc(MAXNAMELEN, sizeof(char)), "calloc (betteria_file1)");

	int size = 0;

	CHECKNULL(block, malloc(size*sizeof(int)), "realloc (betteria_file1)");


	for(size = 25; size <= 12512; size+= 1250){

		CHECKNULL(block, realloc(block, size*sizeof(int)), "realloc (betteria_file1)");

		for(int i = 0; i < size; i++){
			block[i] = i;
		}
		
		sprintf(name_file, "%d_numeri", size);

		( os_store(name_file, block, size*sizeof(int)) == -1) ? print_log("STORE", -1) : print_log("STORE", 0);
	
	}

	frees(name_file, block, NULL);
	return;
}

void batteria_store2(){

	char* name_file; 
	CHECKNULL(name_file, calloc(MAXNAMELEN, sizeof(char)), "calloc (betteria_file2)");

	char* frase = "Mala tempora currunt\n";
	int len = strlen(frase);

	int n = 0;

	char* block;
	CHECKNULL(block, malloc(((len*n)+1)*sizeof(char)), "malloc (batteria_file2)");

	int size;
	char* tmp;

	for(n = 2500; n <= 5000; n += 277){

		size = (len*n)+1;

		CHECKNULL(block, realloc(block, size*sizeof(char)), "malloc (batteria_file2)");

		tmp = block;

		for(int i = 0; i < n; i++){
			memcpy(tmp, frase, len);
			tmp += len;
		}

		// tmp += size;
		*tmp = '\0';

		sprintf(name_file, "%d_frasi", n);

		( os_store(name_file, block, size*sizeof(char)) == -1) ? print_log("STORE", -1) : print_log("STORE", 0);
	}
	
	free(name_file);
	free(block);


	return;
}

void batteria_retrieve(){

	int i;

	/************ RETRIEVE del file "5025_numeri" ************/

	int* file1;

	( (file1 = (int*) os_retrieve("5025_numeri")) == NULL ) ? print_log("RETRIEVE", -1) : print_log("RETRIEVE", 0);

	if( file1 != NULL){

		//verifico che sia corretto
		for(i=0; i<5025; i++){
			if(file1[i] != i) break;
		} 

	}
	

	/************ RETRIEVE del file "4120_frasi" ************/

	char* file2;


	( (file2 = (char*) os_retrieve("4120_frasi")) == NULL ) ? print_log("RETRIEVE", -1) : print_log("RETRIEVE", 0);

	if(file2 != NULL ){

		char* frase = "Mala tempora currunt\n";
		int len = strlen(frase);

		char* tmp = file2;
		int n = 4120;

		for(i = 0; i < n; i++){
				if ( strncmp(tmp, frase, len) != 0) break;
				tmp += len;
		}
	}
	
	char* notexist;

	( (notexist = (char*) os_retrieve("NonEsiste")) == NULL ) ? print_log("RETRIEVE", -1) : print_log("RETRIEVE", 0);

	if( file1 ) free(file1);
	if( file2 ) free(file2);

	return;
}

void batteria_delete(){

	( os_delete("3775_numeri") == -1) ? print_log("DELETE", -1) : print_log("DELETE", 0);

	( os_delete("4993_frasi") == -1) ? print_log("DELETE", -1) : print_log("DELETE", 0);

	( os_delete("NonEsiste") == -1) ? print_log("DELETE", -1) : print_log("DELETE", 0);

	( os_delete("") == -1) ? print_log("DELETE", -1) : print_log("DELETE", 0);

	return;
}



int main(int argc, char *argv[]) {

	CHECKNULL(stats, malloc(sizeof(ClientStat_t)), "malloc (main)");
	memset(stats, 0, sizeof(ClientStat_t));

	initializeStats(stats);

	if(argc != 3){
		fprintf(stderr, "Usage: %s <nome_client> [1|2|3]\n", argv[0] );
		exit(EXIT_FAILURE);
	}

	struct sigaction s;
	memset(&s,0,sizeof(s));    
	s.sa_handler=SIG_IGN;
	if ( (sigaction(SIGPIPE,&s,NULL) ) == -1 ) {   
		perror("sigaction");
		exit(EXIT_FAILURE);
	} 


	char* tmp;
	int test_choice = strtol(argv[2], &tmp, 10);

	if( strchr(argv[1], ' ') != NULL || tmp == argv[2] || *tmp != '\0' || test_choice < 1 || test_choice > 3 || strlen(argv[1]) > MAXNAMELEN) {
		fprintf(stderr, "Usage: %s <nome_client> [1|2|3]\n", argv[0] );
		exit(EXIT_FAILURE);
	}


	strncpy(stats->client_name, argv[1], strlen(argv[1]));


	int ntimes = MAXTRIES;
	int i;

	//provo a connettermi
	for(i=0;i<ntimes;i++){
		if (os_connect(argv[1]) == -1) {
			sleep(1);
			continue;
		}
		break;
	}

	if( i != ntimes){ //connesso
		print_log("REGISTER", 0);
	} else{ //non connesso
		print_log("REGISTER", -1);
		exit(EXIT_FAILURE);
	}


	switch(test_choice) {
		case 1: //STORE di 20 oggetti da 100 a 100000 Byte
			batteria_store1();
			batteria_store2();
			break;

		case 2: //RETRIEVE e verifica dei dati
			batteria_retrieve();
			break;

		case 3: //DELETE
			batteria_delete();
			break;	
	}

	//disconessione
	if (os_disconnect() == -1) { //non a buon fine
		print_log("LEAVE", -1); 
	} else{
		print_log("LEAVE", 0); //a buon fine
	}

	free(stats);
	return 0;


}