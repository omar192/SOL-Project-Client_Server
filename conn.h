#ifndef CONN_H
#define CONN_H



//tempi di attesa select main server
#define SECONDS 1
#define MSECONDS 0

#define SOCKNAME     "./object.store"
#define DATAPATH    "./data"
#define MAXBACKLOG   32
#define MAXCLIENTS 50


// define lunghezza messaggio client to server
#define MAXHEADERSIZE 13 //8 per RETRIEVE + 5 tra spazi e newline
#define MAXNAMELEN 255 //quanto impone max Unix, vale sia per nome Client che nome file
#define MAXDATALEN 10 //1GB => 1073741824 Bytes => 10 cifre per rappresentarlo
#define MESSAGELEN (MAXHEADERSIZE + MAXNAMELEN + MAXDATALEN + 1024)

#define MAXDATASIZE 1073741824

#define MSGERRORLEN 23


//operazioni possibili che il server può ricevere
#define NOPERATIONS 5
typedef enum {
   OP_CLIENT_REGISTER  = 0,
   OP_CLIENT_STORE  = 1,
   OP_CLIENT_RETRIEVE  = 2,
   OP_CLIENT_DELETE  = 3,
   OP_CLIENT_LEAVE  = 4
} op_t;



#endif /* CONN_H */
