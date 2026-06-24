/* === sw-cgi.c - Server Web CGI Prof - HTTP 1.1 ===

	Implementazione ibrida del server "classico" sw.c e
	il server dinamico sw-dinamico.c. Si uniscono le funzionalitá
	permettendo sia di ricevere un file sia di eseguire un
	programma.	
	Se l'URL inizia con /cgi/ entra in modalitá CGI, se no allora il server
	restituisce semplicemente il file richiesto con una GET.
*/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <errno.h>
#include <arpa/inet.h>
#include <stdint.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>

/* 	local e remote sono due struct Socket Address Internet.
	1. local rappresenta le coordinate del server da dove ascolto.
		Viene poi effettuato il binding col socket s.
	2. remote rappresenta le coordinate del client che manda la richiesta
		(ovvero il browser dell'utente che si vuole connettere). Viene inizializzato
		con 0 nella porta e 0 nell'indirizzo IP. Quando un utente manda
		una richiesta allora accept() inserisce i dati corretti relativi a
		porta e indirizzo IP. */
struct sockaddr_in local, remote;
char request[100000];
char response[1000];

struct header {
  char * n;
  char * v;
} h[100];

unsigned char envbuf[1000];
int pid;
int env_i, env_c;
char * env[100];
int new_stdin, new_stdout;	// Non vengono usati...?
char * myargv[10];

/* Spiegazione:
	Ogni volta che il server legge una parte utile dell'header HTTP
	chiama questa funzione. La funzione crea una stringa formattata
	come CHIAVE=VALORE e salva il puntatore a questa stringa
	nell'array globale env. Questo sará il pacco che il server consegnerá
	al programma cgiexe al momento dell'esecuzione. */
void add_env(char * env_key, char* env_value){
                sprintf(envbuf + env_c, "%s=%s", env_key, env_value);
                env[env_i++] = envbuf + env_c;
                env_c += (strlen(env_value) + strlen(env_key) + 2);
                env[env_i] = NULL;
}

// === Main ===
int main() {
	char hbuffer[10000];
	char * reqline;
	char * method, *url, *ver;
	char * filename, *content_type;
	char fullname[200];
	FILE * fin;
	int c;
	int n;
	int i,j,t, s,s2;
	int yes = 1;
	int len;
	int length;

	// === Setup Connessione ===
	if (( s = socket(AF_INET, SOCK_STREAM, 0 )) == -1) { 
		printf("errno = %d\n", errno);
		perror("Socket Fallita");
		return -1;
	}

	local.sin_family = AF_INET;
	local.sin_port = htons(27999);
	local.sin_addr.s_addr = 0;

	t = setsockopt(s,SOL_SOCKET,SO_REUSEADDR,&yes,sizeof(int));
	if (t==-1){
		perror("setsockopt fallita"); 
		return 1;
	}

	if (-1 == bind(s, (struct sockaddr *)&local, sizeof(struct sockaddr_in))) { 
		perror("Bind Fallita"); 
		return -1;
	}

	if (-1 == listen(s,10)) { 
		perror("Listen Fallita"); 
		return -1;
	}

	remote.sin_family = AF_INET;
	remote.sin_port = htons(0);
	remote.sin_addr.s_addr = 0;
	len = sizeof(struct sockaddr_in);

	// === Main Loop ===
	while (1) {
		s2 = accept(s, (struct sockaddr *) &remote, &len);
		bzero(hbuffer, 10000);		// bzero() appartiene a <strings.h>. Spiegazione sotto...
		bzero(h, sizeof(struct header)*100);
		reqline = h[0].n = hbuffer;
		for (i=0,j=0; read(s2,hbuffer+i,1); i++) {
			if(hbuffer[i]=='\n' && hbuffer[i-1]=='\r'){
				hbuffer[i-1]=0; 	// Termino il token attuale
			    if (!h[j].n[0]) break;
					h[++j].n=hbuffer+i+1;
  			}
  			if (hbuffer[i]==':' && !h[j].v){
    			hbuffer[i]=0;
    			h[j].v = hbuffer + i + 1;
  			}
 		}
		length=0;

		for(i=0;i<j;i++){
			printf("%s ---> %s\n",h[i].n,h[i].v);
			if(!strcmp(h[i].n,"Content-Length")){
					length=atoi(h[i].v);	// atoi() converte una str in int
			}

			if(!strcmp(h[i].n,"Content-Type")){
					add_env("CONTENT_TYPE",h[i].v+1);
			}
		}
        len=1000;
        printf("%s\n",reqline);
        method = reqline;
        for(i=0;i<len && reqline[i]!=' ';i++); reqline[i++]=0;
        url=reqline+i;
        for(;i<len && reqline[i]!=' ';i++); reqline[i++]=0;
        ver=reqline+i;
        for(;i<len && reqline[i]!='\r';i++); reqline[i++]=0;
        add_env("METHOD",method);
        filename = url+1;
		// === Blocco CGI ===
        if (!strncmp(url,"/cgi/",5)){
			filename = url+5; //AGGIUNTA MIA
			printf("DEBUG:Filename = %s\n",filename);
			// Richiesta di tipo GET
			if (!strcmp(method,"GET")) {
                for(i=0;filename[i] && (filename != '?');i++) {		// Cerca dopo il '?' per parametri (spiegazione in fondo)
                        if(filename[i]=='?'){
                                filename[i]=0;
                                add_env("QUERY_STRING", filename + i + 1);	// Salva i parametri in una environment variable QUERY_STRING	
                        }
				}
				add_env("CONTENT_LENGTH","0");	// Richieste GET hanno CONTENT_LENGHT = 0 per definizione
			}
			
			// Richiesta di tipo POST
			else if(!strcmp(method,"POST")){			// Se il method é uguale a POST
                        char tmp[10];					// stringa da 10 char
                        sprintf(tmp,"%d",length);		// Inserisce la lunghezza in tmp
                        printf("DEBUG:method = POST CONTENT_LENGTH =  %s\n",tmp);
                        add_env("CONTENT_LENGTH",tmp);	// Aggiunge la lunghezza del contenuto come variabile environment
			} else {	// Richiesta di tipo non implementato
				sprintf(response,"HTTP/1.1 501 Not Implemented\r\n\r\n");
				write(s2,response,strlen(response));
				close(s2);
				continue;
			}

			fin=fopen(filename,"rt");

			// File eseguibile cercato non esiste 
			if (fin == NULL){
				sprintf(response,"HTTP/1.1 404 Not Found\r\n\r\n");
				printf("DEBUG:response = %s\n",response);
				write(s2,response,strlen(response));
			
			// File eseguibile trovato -> Comincia l'esecuzione
			} else {
				sprintf(response,"HTTP/1.1 200 OK\r\n\r\n");	// Invia risposta OK
				printf("DEBUG:response = %s\n",response);
				write(s2,response,strlen(response));
				fclose(fin);
				for (i=0;env[i];i++) {
					printf("environment: %s\n",env[i]);			// Stampa variabili environment
				}
				sprintf(fullname,"./%s",filename);
				printf("DEBUG:fullname = %s\n",fullname);
				myargv[0]=fullname;
				myargv[1]=NULL;
				printf("Executing %s\n",fullname);

				// Sdoppiamento dei processi
				if(!(pid=fork())){		// Il figlio entra nell'if perché per lui fork() ritorna 0
					// dup2 permette di cambiare il valore del file descriptor
					dup2(s2,1);			// FD=1 -> Std Out (stacca il socket stdout e collegalo a socket di rete s2)
					dup2(s2,0);			// FD=0 -> Std In (stacca il socket stdin e collegalo a socket di rete s2)
					// Il processo figlio sovrascrive se stesso, smettendo di eseguire il codice del
					// server web, caricando in memoria cgiexe, passando come parametro l'array env.
					if(-1==execve(fullname,myargv,env)) { 
						perror("execve");
						exit(1);
					}
				}
				waitpid(pid,NULL,0);	// Il processo padre (il server vero) aspetta che il figlio muoia (exit()).
										// Allora il padre chiude il socket e torna in ascolto per altri client.
										// Se un nuovo client fa richieste finisce nella coda della listen().
										// Il server aspetta in idle state che il figlio muoia.
                printf("Il processo figlio e' terminato...\n");
			}

        }	// fine if() CGI
		// === BLOCCO NON CGI ===
        else if (!strcmp(method,"GET")){
			filename = url+1;
			printf("filename: %s\n",filename);
			fin=fopen(filename,"rt");
			// File non trovato
			if (fin == NULL){
				sprintf(response,"HTTP/1.1 404 Not Found\r\n\r\n");
				write(s2,response,strlen(response));
			} else {	// File trovato
				sprintf(response,"HTTP/1.1 200 OK\r\n\r\n");
				write(s2,response,strlen(response));
				while ( (c = fgetc(fin))!=EOF)		write(s2,&c,1);
				fclose(fin);
			}
		} else {
			sprintf(response,"HTTP/1.1 501 Not Implemented\r\n\r\n");
			write(s2,response,strlen(response));
        }
        close(s2);
        env_c=env_i=0;
	}	// fine while(1)
	close(s);
}

/* === SPIEGAZIONI ===
	- filename[i] && (filename[i] != '?'): Il for continua ad andare avanti finché questa espressione non risulta VERA.
			Qualsiasi numero (o carattere) != 0 é considerato VERO, mentre 0 é considerato FALSO.
			Ció significa che affinché il for termini deve fallire l'AND di queste due condizioni:
				- filename[i] NON risiede su un carattere terminatore (basta un qualunque altro carattere)
				- filename[i] NON é il carattere '?'
			Quindi se si incontra un '?' o un carattere terminatore il ciclo finisce. L'obiettivo ovviamente é trovare
			il '?'.
	- strcmp() e strncmp(): Queste funzioni confrontano due stringhe analizzando il valore ASCII, carattere per
							carattere. Se la prima stringa è alfabeticamente precedente rispetto alla seconda allora
							viene ritornato -1, se è alfabeticamente successiva 1 e se sono uguali viene ritornato 0.
							strncmp() permette di confrontare a partire da un certo punto della stringa, mentre strcmp()
							le confronta nella loro interezza.
							Scrivere if (!(strcmp(method, "GET")) equivale a scrivere "se non ci sono differenze tra
							method e "GET" allora..." questo perché se non ci fossero differenze -> return 0 -> FALSE
							e quindi non si entrerebbe nell'if. Invece se viene ritornato 0 -> !0 -> !FALSE -> TRUE.
	- bzero(void *s, size_t n): Scrive n byte (un char praticamente) a 0 nella stringa s. Questo viene eseguito appena
								dopo che viene accettata una connessione per il seguente motivo:
									Assumiamo che si connetta Client1 e che riempia hbuffer con 500 byte.
									Successivamente si connetta Client2 e invii una richiesta di soli 50 byte, che devono
									venire scritti all'inizio di hbuffer. La nuova richiesta sovrascrive i primi 50
									byte della richiesta di Client1 peró i successivi 450 byte rimangono inalterati,
									quindi la richiesta di Client2 sará un frankenstein di 500 byte.
								Per questo motivo bisogna azzerare il buffer ogni volta che si accetta una nuova
									connessione.
*/
