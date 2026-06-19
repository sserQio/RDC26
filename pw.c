/* === pw.c - Proxy Web HTTP 1.1 ===

*/
#include <stdio.h>
#include <stdlib.h>		// per strtol()
#include <string.h>
#include <stdint.h>
#include <unistd.h>		// read() e write()
#include <sys/types.h>	// uuh
#include <sys/socket.h>
#include <errno.h>
#include <arpa/inet.h>	// inet_pton()
#include <netdb.h>
#include <signal.h>

int pid;
struct sockaddr_in local, remote, server;
char request[10000];	// La nuova richiesta HTTP GET formattata e ricostruita dal
						// proxy prima di essere spedita al server finale.

char request2[10000];	// Usato durante il metodo CONNECT per leggere i dati
						// crittografati dal client e inviarli al server.

char response[10000];	// Usato per inviare messaggi di stato HTTP generati
						// direttamente dal proxy verso il client.

char response2[10000];	// Usato durante il metodo CONNECT per leggere le risposte
						// crittografate dal server e rimandarle al client.

struct header {
	char * n;
	char * v;
} h[100];

struct hostent * he;	// Puntatore a una struttura dati che memorizza i risultati
						// della funzione gethostbyname(), ovvero cio' che fa un DNS.
						// Verranno fatte associazioni Hostname - IP risolto.

// === MAIN ===
int main() {
	char hbuffer[10000];// Usato per leggere la richiesta originale del client, un
						// carattere alla volta finche non vengono parsati tutti
						// gli header.

	char buffer[2000];	// Un buffer di appoggio, usato per travasare i dati dal
						// server s3 al client s2.
	char * reqline;
	char * method, *url, *ver, *scheme, *hostname, *port;
	char * filename;

	int i, j, t; 
	int s, s2, s3;		// Socket: ascolto, client, server
	int yes = 1;
	int len;

	// Variabili non utlizzate.
	FILE * fin;
	int c;
	int n;

	// Creazione socket di ascolto.
	if ((s = socket(AF_INET, SOCK_STREAM, 0)) == -1) {
		printf("errno = %d\n", errno);
		perror("Socket fallita");
		return -1;
	}

	// Inizializzazione local
	local.sin_family = AF_INET;
	local.sin_port = htons(20161);
	local.sin_addr.s_addr = 0;

	// Normalmente l'OS tiene la porta occupata per qualche minuto dopo la chiusura
	// del programma. Con la setsockopt, appena il programma termina la porta
	// si libera cosi da poterla utilizzare subito.
	if (-1 == setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(int))) {
		perror("Setsockopt fallita.");
		return 1;
	}

	// Binding tra il socket s e la struttura local.
	if (-1 == bind(s, (struct sockaddr *) &local, sizeof(struct sockaddr_in))) {
		perror("Bind fallita.");
		return -1;
	}
	
	// Avvia l'ascolto con una coda di 10 elementi.
	if (-1 == listen(s, 10)) {
		perror("Listen fallita.");
		return -1;
	}

	// Inizializzazione remote
	remote.sin_family = AF_INET;
	remote.sin_port = htons(0);
	remote.sin_addr.s_addr = 0;

	len = sizeof(struct sockaddr_in);

	while(1) {
		// Viene creato il socket s2 il momento in cui si accetta una connessione.
		// Viene compilata remote con le informazioni del client accettato.
		s2 = accept(s, (struct sockaddr *) &remote, &len);
		printf("Remote address: %.8X\n", remote.sin_addr.s_addr);
		
		/* Creazione processo figlio:
		* - Al padre, fork() restituisce il PID del figlio quindi la condizione
		*	if() é valida: il processo continua tornando all'inizio del while
		*	per fare una nuova accept.
		* - Al figlio, fork() restituisce 0 quindi la condizione if() é falsa:
		*	viene saltato continue e procede a elaborare la richiesta di s2.
		*
		*  In conclusione avremo il processo padre che continua a occuparsi
		*  solamente delle accept() mentre i processi figli si occupano ognuno
		*  di un client a testa.
		*/
		if (fork()) continue;
		if (s2 == -1) {
			perror("Accept fallita.");
			exit(1);
		}
		
		// Il buffer di lettura viene azzerato.
		bzero(hbuffer, 10000);

		// La struttura dati per gli elementi dell'header viene azzerata.
		bzero(h, 100 * sizeof(struct header));

		// Facciamo si che sia reqline sia h[0].n puntino all'indirizzo
		// di memoria iniziale di hbuffer (buffer di lettura).
		reqline = h[0].n = hbuffer;

		// Parser Header
		for (i = 0, j = 0; read(s2, hbuffer + i, 1); i++) {
			printf("%c", hbuffer[i]);
			if (hbuffer[i] == '\n' && hbuffer[i - 1] == '\r') {
				hbuffer[i - 1] = 0;		// Termino il token attuale
				if (!h[j].n[0]) break;
				h[++j].n = hbuffer + i + 1;
			}
			if (hbuffer[i] == ':' && !h[j].v && j > 0) {
				hbuffer[i] = 0;
				h[j].v = hbuffer + i + 1;
			}
		}

		printf("Request line: %s\n", reqline);
		// La request line stampata sará una cosa del genere:
		// GET http://www.example.com/ HTTP/1.1
		// Ogni spazio che incontriamo verrá sostituito con un carattere
		// terminatore di stringa in modo tale da separare in 3 variabili
		// diverse il metodo, l'url e la versione HTTP.
		method = reqline;
		for (i = 0; i < 100 && reqline[i] != ' '; i++); reqline[i++] = 0;

		url = reqline + i;
		for (; i < 100 && reqline[i] != ' '; i++); reqline[i++] = 0;

		ver = reqline + i;
		for (; i < 100 && reqline[i] != '\r'; i++); reqline[i++] = 0;

		// --- METODO GET ---
		if (!strcmp(method, "GET")) {
			// Parsing dello schema (ad es. HTTP)
			scheme = url;
			printf("url = %s\n", url);
			for (i = 0; url[i] != ':' && url[i]; i++);
			if (url[i] == ':')	url[i++] = 0;
			else {
				printf("Parse error, expected ':'");
				exit(1);
			}
			
			// Parsing dell'hostname (ad es. www.example.com)
			if (url[i] != '/' || url[i + 1] != '/') {
				printf("Parse error, expected '//'");
				exit(1);
			}
			i = i + 2;
			hostname = url + i;
			
			// Parsing del filename o del percorso (ad es. index.html)
			for (; url[i] != '/' && url[i]; i++);
			if (url[i] == '/') url[i++] = 0;
			else {
				printf("Parse error, expected '/'");
				exit(1);
			}
			filename = url + i;
			
			// Stampa della richiesta GET parsata.
			printf("Schema: %s, hostname: %s, filename: %s\n", scheme, hostname, filename);

			// Risoluzione DNS dell'hostname
			// Prende l'hostname e restituisce la struttura hostent (salvata
			// nel puntatore he). Dentro questa struttura é contenuto
			// l'indirizzo IP reale del server, pronto per essere usato.
			he = gethostbyname(hostname);
			if (he == NULL) {
				printf("gethostbyname fallita.\n");
				return 1;
			}

			printf("%d.%d.%d.%d\n", (unsigned char) he -> h_addr[0], (unsigned char) he -> h_addr[1], (unsigned char) he -> h_addr[2], (unsigned char) he -> h_addr[3]);

			// Creazione socket server
			if ((s3 = socket(AF_INET, SOCK_STREAM, 0)) == -1) {
				printf("errno = %d\n", errno);
				perror("Socket fallita.");
				exit(-1);
			}

			server.sin_family = AF_INET;
			server.sin_port = htons(80);	// Porta traffico HTTP
			server.sin_addr.s_addr = *(unsigned int *)(he -> h_addr);

			// Connect tra proxy e server di destinazione
			if (-1 == connect(s3, (struct sockaddr *) &server, sizeof(struct sockaddr_in))) {
				perror("Connect fallita.");
				exit(1);
			}

			// Il proxy non inoltra la richiesta originale ma ne
			// produce una nuova.
			sprintf(request, "GET /%s HTTP/1.1\r\nHost:%s\r\nConnection: close\r\n\r\n", filename, hostname);
			printf("%s\n", request);

			// Invia la richiesta al server di destinazione.
			write(s3, request, strlen(request));

			// Finché c'è da leggere (quindi t > 0), il proxy legge
			// depositando al massimo un blocco da 2000 byte nel buffer.
			// Il buffer poi viene immediatamente travasato verso il
			// client (in mandante originale della richiesta).
			while (t = read(s3, buffer, 2000)){
				write(s2, buffer, t);
			}
			close(s3);						// Non c'é piú da leggere, chiude il socket.

		// --- METODO CONNECT ---
		} else if (!strcmp("CONNECT", method)) {
			// Parsing dell'hostname (ad es. www.example.com)
			// Quando un browser usa CONNECT non manda l'indirizzo completo (https://...)
			// ma manda solo la destinazione e la porta (www.google.com:443)
			hostname = url;
			for (i = 0; url[i] != ':'; i++); url[i] = 0;
			port = url + i + 1;
			printf("Hostname:%s, Port:%s\n", hostname, port);

			// Risoluzione DNS dell'hostname
			he = gethostbyname(hostname);
			if (he == NULL) {
				printf("gethostbyname fallita.\n");
				return 1;
			}

			printf("Connecting to address: %u.%u.%u.%u\n", (unsigned char) he -> h_addr[0], (unsigned char) he -> h_addr[1], (unsigned char) he -> h_addr[2], (unsigned char) he -> h_addr[3]);

			// Creazione socket server
			if ((s3 = socket(AF_INET, SOCK_STREAM, 0)) == -1) {
				printf("errno = %d\n", errno);
				perror("Socket fallita.");
				exit(-1);
			}

			server.sin_family = AF_INET;

			// La porta nella GET era fissa a 80, qui si usa la funziona atoi()
			// per convertire la porta ricevuta sotto forma di testo in un
			// numero utilizzabile dal socket.
			server.sin_port = htons((unsigned short) atoi(port));
			server.sin_addr.s_addr = *(unsigned int*) he -> h_addr;

			// Si esegue la connect sul socket s3
			if (-1 == connect(s3, (struct sockaddr *) &server, sizeof(struct sockaddr_in))) {
				perror("Connect to server fallita.");
				return 1;
			}

			sprintf(response, "HTTP/1.1 200 Established\r\n\r\n");
			write(s2, response, strlen(response));
			
			// Spiegazione di questo blocco in fondo.
			if (!(pid = fork())) {	// Child
				while (t = read(s2, request2, 2000)) {
					write(s3, request2, t);
				}
				exit(0);
			} else {				// Parent
				while (t = read(s3, response2, 2000)) {
					write(s2, response2, t);
				}
				kill(pid, SIGTERM);
				close(s3);
			}
		}
		// --- METODO NON IMPLEMENTATO ---
		else {
			sprintf(response, "HTTP/1.1 501 Not Implemented\r\n\r\n");
			write(s2, response, strlen(response));
		}
		close(s2);
		exit(1);
	}	// fine while(1)
	close(s);
}


/* --- SPIEGAZIONI ---

	- Spiegazione blocco fork() in CONNECT:
		A questo punto del programma bisogna rimanere in ascolto anche dal client (browser che
		comunica dal socket s2) ma, siccome la funzione read() é bloccante, se il programma
		rimanesse in ascolto su s2, si perderebbe quello che potrebbe ricevere in entrata da
		s3 (ovvero dal server). In HTTPS, server e client "parlano" contemporaneamente,
		quindi servono due canali di comunicazioni separati.
		Per questo motivo bisogna sdoppiare nuovamente il programma.

		La fork() crea un processo figlio, dedicato al traffico che parte dal client: vediamo
		che il processo figlio é in ascolto sul socket s2 e finché c'é qualcosa da leggere,
		legge 2000 byte e li invia al server (sul socket s3). Quando il client non ha piú nulla da inviare
		e la read risulta 0, il ciclo si interrompe e il processo figlio termina silenziosamente
		con exit(0).

		Mentre il figlio si occupa del traffico che parte dal client, il processo padre si occupa
		del traffico che parte dal server. Si blocca in ascolto sul socket s3 e ogni volta
		che il socket risponde, inoltra la risposta al socket s2 (al client).
		Se il server chiude la connessione (la read() ritorna 0), allora il processo padre
		uccide il processo figlio (pid): questo é importante perché se non lo uccidesse, il
		processo figlio potrebbe rimanere bloccato all'infinito ad aspettare dati dal browser in 
		un tunnel ormai morto a metà. Infine, il padre chiude il socket s3.
*/
