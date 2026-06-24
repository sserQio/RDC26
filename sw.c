/* === sw.c - Server Web Prof (riordinato da me) - HTTP 1.1 ===

	
*/
#include <stdio.h>
#include <string.h>
// #include <sys/socket.h>
// #include <netinet/in.h>
#include <netinet/ip.h> // include le due precedenti
#include <sys/types.h>          
#include <sys/socket.h>
#include <unistd.h>
#include <arpa/inet.h>	// per inet_pton()

// --- GLOBAL STRUCTS ---
struct sockaddr_in server;

struct header {
	char * n;
	char * v;
	} h[100];

// --- FUNZIONI AUSILIARIE ---
// girashort() è sostitutiva artigianale di htons()
short int girashort (short int a){
	short int b = 1;
	char *p = (char*) &b;
    // printf("girashort %x\n",a);
    // printf("girashort %x\n",a<<8 &0xffff);
    // printf("girashort %x\n",(a>>8 & 0x00ff));
	if(*p) return (a<<8&0xffff) + (a>>8 & 0x00ff);
	return a;
}

int sonouguali ( char * s1, char * s2 ) {
    int i = 0;
    do if (s1[i] != s2[i]) return 0; while ((s1[i++]!=0)) ;
    return 1;
}

int stringaintero ( char * s){
	int tot=0;
	for(int i =0; (s[i]>='0') && (s[i]<='9'); i++)
		tot = tot * 10 + s[i]-'0' ;
	return tot;
}

// --- BUFFER GLOBALI ---
char hbuf[10000];			// Header Buffer: Utilizzato per memorizzare carattere per carattere l'header
							// 				   della richiesta in arrivo dal client.
char entitybody[1000000];	// Buffer destinato a contenere il body della richiesta HTTP, ovvero
							// i dati inviati dal client.

// --- MAIN ---
int main() {
	char response[5000];			// Buffer temporaneo. Riempito con i pezzi del file richiesto dal
									// client per poi essere inviati alla rete. 
	FILE * f;						// Inizializzato dalla funzione fopen() e serve per aprire, leggere
									// e gestire il file richiesto dal client.
	struct sockaddr_in client;		// Struct popolata dalla funzione accept(). Al suo interno vengono
									// salvati dati del client (IP e porta d'origine).
	int i,j;						// Iteratori
	int t,counter;					// Counter
	int lunghezza;			// lunghezza: Usata per indicare la dimensione della struct client
									// e passata ad accept().
	int s,s2;		// s = Listening Socket, attende nuove connessioni in entrata (creato con socket())
					// s2 = Connected Socket, restituito dalla funzione accept() ogni volta che un client
					// 		si collega. Tutte le comunicazioni (lettura richiesta e invio file) avvengono 
					//		su questo socket.
	int yes = 1;
	int n;									// n memorizza il numero di byte che la funzione fread() è 
											// riuscita effettivamente a leggere dal file in un singolo 
											// passaggio. Viene usato per dire alla write() quanti byte 
											// esatti spedire al client.

	// - Variabili per Parsing -
	char primiDuePunti;						// Variabile bool per capire se sulla riga corrente é giá stato
											// incontrato il char ':'
	unsigned char * method, *url, *ver;		// method: Puntatore che isola la stringa del metodo HTTP
											//			estratto dalla request line (es. GET, POST)
											// url: Puntatore che isola il percorso del file richiesto
											//		estratto sempre dalla request line (es. /index.html)	
											// ver: Puntatore che isola la versione del protocollo
											//		utilizzata
	char * rl;	// Request Line: È un puntatore che punta alla prima riga in assoluto della richiesta HTTP
				// 				(ad esempio GET /index.html HTTP/1.1) che nella struttura logica corrisponde al 
				//				primo nome (h[0].n).

	// - Variabili non utilizzate - Legacy -
	unsigned char req[1000000];		// Non utilizzata
	int x;							// Non utilizzata
	unsigned char * p;				// Non utilizzata

	char * resp_500 = "HTTP/1.1 500 Internal Server Error\r\nContent-Length:39\r\n\r\n<html><H1>Torno Subito!</H1><br></html>";
	char * resp_404 = "HTTP/1.1 404 File not found\r\n\r\n";
	char * resp_200 = "HTTP/1.1 200 OK \r\nConnection:closed\r\n\r\n";

	if ((s = socket(AF_INET, SOCK_STREAM, 0)) == -1) {		// Inizializzazione Socket
		perror("Socket Fallita");
		return 0;
	}

	// --- Socket Options ---
	// s: socket in ascolto.
	// SOL_SOCKET: indica che stiamo configurando un'opzione a livello base del socket.
	// SO_REUSEADDR: permette di riutilizzare la porta (spiegazione alla fine).
	// &yes = 1: viene passato il valore vero, abilita l'opzione.
	// sizeof(int): come per bind() e accept() le API dei socket richiedono la dimensione esatta di &yes.
	t = setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(int));
	if (t == -1) {
		perror("setsockopt Failed");
		return 1;
	}

	// --- Address and Port configuration ---
	// server.sin_family = AF_INET;
	// server.sin_addr.s_addr = 0;
	// Metodo sopra funziona ma non è preferibile. s.addr = 0 consente l'utilizzo di ogni NIC per connessioni in entrata.
	// Con inet_pton scegliamo l'IP LOCALE di destinazione.
	server.sin_port = htons(8008);
	if (-1 == inet_pton(AF_INET, "127.0.0.1", &server.sin_addr)) {
		perror("IP Address Configuration Failed");
		return 1;
	} else {
		printf("IP Address and Port Configuration successful.\n");
	}

	// --- Bind ---
	if (-1 == bind(s, (struct sockaddr *) &server, sizeof(struct sockaddr_in))){
		perror("Bind failed");
		return 0;
	} else {
		printf("Binding successful.\n");
	}

	// --- Listen ---
	if (-1 == listen(s, 5)){
		perror("Listen failed");
		return 0;
	} else {
		printf("Listen Stack initialization successful.\n");
	}

	while (1) {
		lunghezza = sizeof(struct sockaddr_in);
		if(-1 == (s2 = accept(s, (struct sockaddr *) &client, &lunghezza))) {	// --- Accept ---
			 perror("Accept failed");
			 return 0;
		}

		// --- Header Parser ---
		h[0].n = hbuf+1;
		hbuf[0]=0;
		primiDuePunti=0;
		for (j=0,i=1; read(s2,hbuf+i,1);i++) {
		   if (hbuf[i]==':' && !primiDuePunti){
			  hbuf[i]='\0';
			  h[j].v=hbuf+i+1;
			  primiDuePunti=1;
		   }
		   if (hbuf[i]=='\n' && hbuf[i-1]=='\r') {
			   hbuf[i-1]=0;
			   if(!h[j].n[0]) break;
			   h[++j].n=hbuf+i+1;
			   primiDuePunti=0;
			}
		}

		// --- Entity Body Lenght Parser ---
		lunghezza = 0;
		for(i=0; h[i].n[0]; i++){
			if(sonouguali(h[i].n, "Content-Length")) {
				lunghezza = stringaintero(h[i].v+1);
				printf("lunghezza = %d\n", lunghezza);
			}
			printf("%s ---> %s\n",h[i].n,h[i].v);
		}

		// --- Lettura e Stampa dell'entitybody ---
		for(counter = 0; (t = read(s2,entitybody+counter,lunghezza-counter)); counter += t);
		entitybody[counter] = 0;					// Aggiunge terminatore alla fine della stringa entitybody
		printf("Entity body : %d\n", counter);		// Lunghezza in caratteri dell'entitybody
		printf("%s\n", entitybody);

		rl=h[0].n;						// Parsing della request line (primo oggetto della struttura)
		printf("=== REQUEST ===\n");
		printf("rl=%s\n",rl);

		// --- Parsing del metodo della richiesta ---
		method = rl; 					// Fa puntare method all'inizio di rl (quindi alla G di GET)
		for(i=0;rl[i] != ' ';i++);		// Scorre in avanti fino ad uno spazio
		rl[i++] = 0;					// Imposta carattere terminatore di stringa
		printf("method=%s\n", method);	// method ora è "GET\0"

		// --- Parsing url della richiesta ---
		url = rl+i;
		for( ; rl[i]!=' '; i++);
		rl[i++]=0;
		printf("url=%s\n",url);

		// --- Parsing della versione della richiesta ---
		ver = rl+i;
		printf("ver=%s\n", ver); //ha già il terminatore...

		printf("method = %s, url=%s, ver =%s\n", method, url, ver);

		// --- Lettura del File ---	
		f = fopen(url + 1, "r");	// /index.html, skippa il '/' per cercare nel path corrente
									// altrimenti cercherebbe in root directory. "r" read mode.
		if (f == NULL) {			// Se fopen non trova o non può accedere al file, ritorna NULL
			write(s2, resp_404, strlen(resp_404));
		} else {					// File trovato
			write(s2, resp_200, strlen(resp_200));	// Spiegazione sotto
			do {
				n = fread(response, 1, 5000, f);
				write(s2, response, n);
			} while (n == 5000);
		}

		close(s2);	// Chiusura socket
	} // parentesi del while(1)
}

/* --- SPIEGAZIONI ---

	- Spiegazione SO_REUSEADDR:
		Quando spegni il server (ad esempio premendo Ctrl+C nel terminale) o se il programma va in crash, il sistema operativo (Linux) non libera immediatamente la porta di rete (la 8005 in questo caso). 
		La tiene "prenotata" per un paio di minuti in uno stato chiamato TIME_WAIT, per assicurarsi che tutti i pacchetti ritardatari su quella connessione vengano smaltiti.
		Se provi a riavviare il server in quel lasso di tempo, la funzione bind() fallirà dandoti un errore tipo "Address already in use" (Indirizzo già in uso).
		L'opzione SO_REUSEADDR dice al sistema operativo: "Se questa porta è nello stato TIME_WAIT a causa di una chiusura recente, permettimi di riutilizzarla (Re-Use Address) ignorando il blocco".

	- Spiegazione Lettura file
		Se il file esiste quindi si invia subito la conferma al client con la resp_200 (header 200 OK) usando write.
		Ciclo do-while per inviare il file "a fette".
		Funzione fread():
			size_t fread(size_t size, size_t n; void ptr[restrict size * n], size_t size, size_t n, FILE *restrict stream); 
			The function fread() reads n items of data, each size bytes long from the stream pointed to by stream, storing them at the location given by ptr.
		Quindi fread() cerca di leggere 5000 byte alla volta e li salva nel buffer response. La funzione restituisce quanti byte è riuscita a leggere e salva questo numero in n.
		Poi write() prende gli n byte appena letti (si trovano in response) e li manda in rete (attraverso il socket s2).
		Il ciclo continua finchè ci sono buffer "pieni" da 5000 byte. Non appena si incontra un response <5000 significa che è l'ultimo e quindi il ciclo si interrompe.

	- Spiegazione inet_pton
		Anzichè assegnare manualmente i valori nei campi della struct, facendo come viene mostrato nei commenti, si assegna manualmente solamente la porta locale del server.
		Famiglia degli indirizzi e indirizzo IP del server vengono invece impostati con la funzione inet_pton() (libreria <arpa/inet.h>) che ha come campi:
		inet_pton(FAMIGLIA_INDIRIZZI, "indirizzo ip sotto forma di stringa", &indirizzo di memoria del campo della struct relativo all'indirizzo IP);
*/
