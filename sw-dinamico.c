/* === sw-dinamico.c - Server Web Dinamico Prof (riordinato da me) - HTTP 1.1 ===

		Questa versione del server consente non solo di ricevere file tramite
		la GET ma di impartire comandi grazie alla CGI, effettivamente
		eseguendo comandi nella shell del server. Per esempio eseguire il
		server e successivamente digitare come URL:
		ip:porta/cgi/ls
		Al posto di una pagina web tradizionale verrá stampato su browser la
		lista dei file della directory in cui il file sw-dinamico.c é salvato.
*/
#include <stdio.h>
#include <stdlib.h>		// for system()
#include <string.h>
// #include <sys/socket.h>
// #include <netinet/in.h>
#include <netinet/ip.h> // superset of previous two
#include <sys/types.h>          
#include <sys/socket.h>
#include <unistd.h>
#include <arpa/inet.h>	// for inet_pton()

struct sockaddr_in server;

// --- FUNZIONI AUSILIARIE ---
// girashort() è sostitutiva artigianale di htons()
short int girashort (short int a){
	short int b = 1;
	char *p = (char*) &b;
    //printf("girashort %x\n",a);
    // printf("girashort %x\n",a<<8 &0xffff);
    // printf("girashort %x\n",(a>>8 & 0x00ff));
	if(*p) return (a<<8&0xffff) + (a>>8 & 0x00ff);
	return a;
}

struct header{
	char * n;
	char * v;
	} h[100];

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

// --- GLOBAL BUFFERS ---
char hbuf[10000];
char entitybody[1000000];
int t,counter;

// --- MAIN ---
int main() {
	char response[5000];
	FILE * f;		// per funzione fopen()
	struct sockaddr_in client;
	char primiDuePunti;
	int i,j,lunghezza,lungh;
	int s,s2;
	int yes = 1;
	int n;
	unsigned char * p;
	unsigned char * method, *url, *ver;
	int x;
	int t;
	char * rl; // request_line
	unsigned char req[1000000];
	char * resp_500 = "HTTP/1.1 500 Internal Server Error\r\nContent-Length:39\r\n\r\n<html><H1>Torno Subito!</H1><br></html>";
	char * resp_404 = "HTTP/1.1 404 File not found\r\n\r\n";
	char * resp_200 = "HTTP/1.1 200 OK \r\nConnection: closed\r\n\r\n";
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
	server.sin_port = htons(8005);
	if (-1 == inet_pton(AF_INET, "10.70.93.72", &server.sin_addr)) {
		perror("IP Address Configuration Failed");
		return 1;
	} else {
		printf("IP Address and Port Configuration successful.\n");
	}

	// --- Bind ---
	if(-1 == bind(s, (struct sockaddr *) &server, sizeof(struct sockaddr_in))){
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

	while (1){
		lunghezza = sizeof(struct sockaddr_in);
		if( -1 == (s2 = accept(s,(struct sockaddr *) &client, &lunghezza))) {	// --- Accept ---
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
		lungh = 0;
		for(i=0; h[i].n[0]; i++){
			if(sonouguali(h[i].n, "Content-Length")) {
				lunghezza = stringaintero(h[i].v+1);
				printf("lunghezza = %d\n", lunghezza);
			}
			printf("%s ---> %s\n",h[i].n,h[i].v);
		}

		// --- Lettura e Stampa dell'entitybody ---
		for(counter = 0; (t = read(s2,entitybody+counter,lungh-counter)); counter += t);
		entitybody[counter] = 0;		// Aggiunge terminatore alla fine della stringa entitybody
		printf("Entity body : %d\n", counter );		// Lunghezza in caratteri dell'entitybody
		printf("%s\n", entitybody);

		rl=h[0].n;		// Parsing della request line (primo oggetto della struttura)
		printf("rl=%s\n",rl);

		// --- Parsing del metodo della richiesta ---
		method = rl; 	// Fa puntare method all'inizio di rl (quindi alla G di GET)
		for(i=0;rl[i] != ' ';i++);	// Scorre in avanti fino ad uno spazio
		rl[i++] = 0;		// Imposta carattere terminatore di stringa
		printf("method=%s\n", method);	// method ora è "GET\0"

		// --- Parsing url della richiesta ---
		url = rl+i;
		for(;rl[i]!=' ';i++);
		rl[i++]=0;
		printf("url=%s\n",url);

		// --- Parsing della versione della richiesta ---
		ver = rl+i;
		printf("ver=%s\n", ver); //ha già il terminatore...

		printf("method = %s, url=%s, ver =%s\n", method, url, ver);

		// --- Logica Common Gateway Interface ---
		if (0 == strncmp(url, "/cgi/", 5)) {		// Controlla se i primi 5 char della stringa url sono /cgi/
													// 0 == caso affermativo
			char cmd[200];
			sprintf(cmd, "%s > tmp\n", url + 5);
			sprintf(url, "/tmp");
			printf("Il comando è %s", cmd);
			system(cmd);
		}

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

		// Invia la risposta usando strlen() per sapere esattamente quanti byte inviaresulla rete
		write(s2,response, strlen(response));
		close(s2);	// Chiusura del socket

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

	- Spiegazione CGI
		Se l'utente richiede un URL che comincia con /cgi/, il server non cerca un file da restituire ma prende il resto dell'URL e lo tratta come un comando del terminale.
		Esegue il comando e restituisce l'output al client.
		Successivamente si deve creare la stringa che rappresenta il comando da lanciare al sistema:
			- chiamando http://localhost:8005/cgi/ls allora
				- url = /cgi/ls
				- url + 5 = ls
			- la funzione sprintf() costruisce la stringa cmd[200] = "ls > tmp\n"
			- facendo "> tmp" stiamo indirizzando output verso un file locale chiamato tmp
			- successivamente sovrascrive la variabile url con /tmp
			- poi facendo f = fopen(url + 1, "r"); si apre il file tmp che contiene l'output del comando eseguito e lo invia al browser
			- system(cmd) infine esegue il comando cmd nell'OS

*/
