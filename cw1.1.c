#include <stdio.h>
#include <stdlib.h>				// for strtol()
#include <netinet/ip.h>			// superset of <sys/socket.h> and <netinet/in.h>
#include <errno.h>				// for perror and errno
#include <string.h>
#include <arpa/inet.h>			// for inet_pton()
#include <unistd.h>				// read() and write()

#define CHUNKED -2
#define ENTITY_SIZE 100000

int connection, i, j, t, chunk_size, length;
struct sockaddr_in server;		// Destination Server
char entitybody[2000000], primiDuePunti, hbuf[10000], check[2], chunk[1000000];	// hbuf: buffer that recieves the header
struct header {
	char * n;		// Name
	char * v;		// Value
} h[100];

// String to Integer Conversion Function
int stringaIntero(char * str){
	int tot = 0;
	for (int i = 0; (str[i] >= '0') && (str[i] <= '9'); i++)
		tot = tot * 10 + str[i] - '0';		// Makes use of ASCII value of chars
		/*  Example:
		 *	'9': ASCII value 57
		 *  '0': ASCII value 48
		 * 	'9' - '0' = 57 - 48 = 9 */
	return tot;
}

int main() {

	// Socket Creation
	int tcp_socket = socket(AF_INET, SOCK_STREAM, 0);
	if (tcp_socket == -1) {
		perror("Socket Creation Error");
		return 1;
	} else {
		printf("Socket Creation Successful\n");	
	}
	printf("File Descriptor Index: %d\n", tcp_socket);

	const char * req = "GET / HTTP/1.1\r\n\r\n";

	// Destination Server Declaration
	server.sin_family = AF_INET;
	server.sin_port = htons(80);		// Host To Network Short
	if (inet_pton(AF_INET, "172.217.23.67", &server.sin_addr) <= 0) {	// Set destination server ip
		perror("Invalid IP Address");
		return 1;
	}

	// Connection
	connection = connect(tcp_socket, (struct sockaddr*) &server, sizeof(struct sockaddr_in));
	if (connection == -1) {
		perror("Failed Connection");
		return 1;
	} else {
		printf("Connection established\n");
	}

	// Communication
	for (i = 0; (t = write(tcp_socket, req + i, strlen(req) - i)); i += t);

	// Parser for Header
	h[0].n = hbuf+1;
	hbuf[0] = 0;			// First value is null, for safety reasons
	primiDuePunti = 0;
	for (j = 0,i = 1; read(tcp_socket, hbuf+i, 1); i++){
	   if (hbuf[i] == ':' && !primiDuePunti){
		  hbuf[i] = '\0';
		  h[j].v = hbuf + i + 1;
		  primiDuePunti = 1;
	   }
	   if (hbuf[i] == '\n' && hbuf[i - 1] == '\r') {
		   hbuf[i - 1] = 0;
		   if(!h[j].n[0]) break;
		   h[++j].n = hbuf + i + 1;
		   primiDuePunti = 0;
		}
	}

	// Prints Header
	for(i = 0; h[i].n[0]; i++) {
		printf("%s ---> %s\n", h[i].n, h[i].v);
		if (!strcmp(h[i].n, "Content-Length")) {
			length = atoi(h[i].v);
		}
		if (!strcmp(h[i].n, "Transfer-Encoding")) {
			length = CHUNKED;
		}
	}

	printf("Transfer Method: %d\n", length);

	// CHUKED CASE
	if (length == CHUNKED) {
		for (i = 0, j = 0, chunk_size = -1; chunk_size != 0;) {
			// Undefined behavior nel ciclo seguente - Non importa, il codice ha scopo didattico
			for (i = 0; i < 10 && read(tcp_socket, chunk + i, 1) && chunk[i - 1] != '\r' && chunk[i] != '\n'; i++);
			chunk[i] = 0;
			printf("Extracting chunk size (str): %s\n", chunk);
			chunk_size = (int) strtol(chunk, NULL, 16);
			printf("Extracting chunk size (int): %d\n", chunk_size);

			for (i = 0; i < ENTITY_SIZE && (t = read(tcp_socket, entitybody + j, chunk_size - i)); i += t, j += t);  

			// Consumo il CRLF alla fine del chunk
			i = read(tcp_socket, check, 2);

			printf("Check del CRLF dopo il dati del chunk\n");
			if (i != 2 || check[0] != '\r'|| check[1] != '\n') {
				printf("Error reading last CRLF in chunk body\n");
				return -1;
			}
		}
	entitybody[j] = 0;
	}

	// CONTENT-LENGTH CASE
	if (length != CHUNKED) {
		for (i = 0; i < length && (t = read(tcp_socket, entitybody + i, ENTITY_SIZE - i)); i += t);
		entitybody[i] = 0;
	}

	printf("%s\n", entitybody);
	return 0;
}

/* 
	1. Leggere la dimensione del prossimo chunk
	2. Trasformare in int
		2.a Se > 0 procedere
		2.b Se == 0 terminare
	3. Eseguire read di dimensione chunkint
	4. 
*/
