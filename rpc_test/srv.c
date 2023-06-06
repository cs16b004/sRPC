#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <fcntl.h>
#define PORT 8501
#define BUFFER_SIZE 1500

int set_nonblocking(int fd) {
    int ret = fcntl(fd, F_GETFL, 0);
    if (ret != -1) {
        ret = fcntl(fd, F_SETFL, ret | O_NONBLOCK);
    }
    return ret;
}
int main() {
    int sockfd;
    struct sockaddr_in serverAddr, clientAddr;
    char buffer[BUFFER_SIZE];

    // Create socket
    sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd < 0) {
        perror("Unable to create socket");
        exit(EXIT_FAILURE);
    }

    // Initialize server address structurei
	const char *ipAddressStr = "172.19.0.121";
    in_addr_t ipAddress;

    // Convert string IP address to in_addr_t
    ipAddress = inet_addr(ipAddressStr);
    memset(&serverAddr, 0, sizeof(serverAddr));
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(PORT);
    serverAddr.sin_addr.s_addr = htonl(INADDR_ANY);

    // Bind socket to server address
    if (bind(sockfd, (struct sockaddr *)&serverAddr, sizeof(serverAddr)) < 0) {
        perror("Binding failed");
        exit(EXIT_FAILURE);
    }
    //set_nonblocking(sockfd);
    printf("UDP server is running and waiting for incoming messages...\n");

    while (1) {
        socklen_t addrLen = sizeof(clientAddr);

        // Receive message from client
        int numBytes = recvfrom(sockfd, buffer, BUFFER_SIZE, 0, (struct sockaddr *)&clientAddr, &addrLen);
         if (numBytes < 0) {
             perror("Error receiving2 message");
            // exit(EXIT_FAILURE);
         }
		printf("Bytes received %d",numBytes);
        // Null-terminate the received message
        buffer[numBytes] = '\0';

        printf("Received message from %s:%d\n", inet_ntoa(clientAddr.sin_addr), ntohs(clientAddr.sin_port));
        printf("Message: %s\n", buffer);

        // Reply to client
        const char *replyMessage = "Message received";
        if (sendto(sockfd, replyMessage, strlen(replyMessage), 0, (struct sockaddr *)&clientAddr, addrLen) < 0) {
            perror("Error sending reply");
            exit(EXIT_FAILURE);
        }
    }

    // Close the socket
    close(sockfd);

    return 0;
}
