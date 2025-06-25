/*
=====================================
Assignment 4 Submission
Name: Dev Butani
Roll number: 22CS30022
=====================================
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include "ksocket.h"

int main(int argc, char* argv[]) {
    // initialize ksocket
    int M2 = k_socket(AF_INET, SOCK_KTP, 0);

    // set source and destination IP and port
    char* IP_2 = "127.0.0.1";
    int Port_2 = 6000;
    if(argc == 3) Port_2 = atoi(argv[1]);
    char* IP_1 = "127.0.0.1";
    int Port_1 = 5000;
    if(argc == 3) Port_1 = atoi(argv[2]);

    struct sockaddr_in user1addr, user2addr;
    user1addr.sin_family = AF_INET; 
    user1addr.sin_addr.s_addr = inet_addr(IP_1); 
    user1addr.sin_port = htons(Port_1); 
    user2addr.sin_family = AF_INET; 
    user2addr.sin_addr.s_addr = inet_addr(IP_2); 
    user2addr.sin_port = htons(Port_2); 

    // bind ksocket
    k_bind(M2, &user2addr, &user1addr);

    // receive file contents from user1
    char filename[50];
    sprintf(filename, "received_file_%d.txt", Port_2);
    int fd = open(filename, O_CREAT | O_WRONLY, 0766);
    char buff[BLOCKSIZE];
    int len;
    // receive and write till EOF marker
    while(1) {
        while((len = k_recvfrom(M2, buff, sizeof(buff), 0, NULL, NULL)) < 0);
        if(buff[0] == '%') break;
        if(buff[sizeof(buff)-1] == '\0') len = strlen(buff);
        write(fd, buff, len);
    }
    close(fd);

    bzero(buff, sizeof(buff));
    buff[0] = '%';
    // send end confirmation
    k_sendto(M2, buff, sizeof(buff), 0, &user1addr, sizeof(user1addr));
    sleep(10);

    // close ksocket
    k_close(M2);

    return 0;
}