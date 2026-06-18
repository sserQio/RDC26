#include <stdio.h>
#include <stdlib.h>				// for strtol()
#include <netinet/ip.h>			// superset of <sys/socket.h> and <netinet/in.h>
#include <errno.h>				// for perror and errno
#include <string.h>
#include <arpa/inet.h>			// for inet_pton()
#include <unistd.h>				// read() and write()

int connection, i, j;
struct sockaddr_in server;		// Destination Server
char entitybody[2000], primiDuePunti, hbuf[10000];	// hbuf: buffer that recieves the header
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

	const char * req = "GET /page1.html HTTP/1.1\r\n\r\n";

	// Destination Server Declaration
	server.sin_family = AF_INET;
	server.sin_port = htons(80);		// Host To Network Short
	if (inet_pton(AF_INET, "142.251.140.110", &server.sin_addr) <= 0) {	// Set destination server ip
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
	for (i = 0; (connection = write(tcp_socket, req + i, strlen(req) - i)); i += connection);

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
	// for(i = 0; h[i].n[0]; i++)	 printf("%s ---> %s\n", h[i].n, h[i].v);

	char * chunk;
	int chunkint;
	i = 0;
	while (connection > 0) {
		if ((entitybody[i + 4] == '\n') && (entitybody[i + 5] == '\r')) {
			chunk = entitybody;
			printf("Next chunk %s\n", chunk);
		}
		chunkint = strtol(chunk, NULL, 16);
		connection = read(tcp_socket, entitybody, chunkint);
		chunk += chunkint;
	}

	entitybody[i] = 0;	// String terminator, for printf
	printf("%s", entitybody);
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
