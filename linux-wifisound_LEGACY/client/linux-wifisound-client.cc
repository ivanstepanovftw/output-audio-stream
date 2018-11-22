/*
 * EEEEEE ROCK!!!
 */
#define PORT 59011
#define GROUP "225.0.0.37"
#define MSGBUFSIZE 256

#include <iostream>
#include <thread>
#include <enet/enet.h>
#include <sys/socket.h>
#include <cstring>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

using namespace std;

int main(int argc, char *argv[]) {
	struct sockaddr_in addr;
	int fd, cnt;
	struct ip_mreq mreq;
	char *message = "Hello, World!";
	
	/* create what looks like an ordinary UDP socket */
	if ((fd = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
		perror("socket");
		exit(1);
	}
	
	/* set up destination address */
	memset(&addr, 0, sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_addr.s_addr = inet_addr(GROUP);
	addr.sin_port = htons(PORT);
	
	/* now just sendto() our destination! */
	while(1) {
		if (sendto(fd, message, sizeof(message), 0, (struct sockaddr *) &addr,
		           sizeof(addr)) < 0) {
			perror("sendto");
			exit(1);
		}
		sleep(1);
	}
}
