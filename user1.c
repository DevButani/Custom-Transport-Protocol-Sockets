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
    int M1 = k_socket(AF_INET, SOCK_KTP, 0);

    // set source and destination IP and port
    char* IP_1 = "127.0.0.1";
    int Port_1 = 5000;
    if(argc == 3) Port_1 = atoi(argv[1]);
    char* IP_2 = "127.0.0.1";
    int Port_2 = 6000;
    if(argc == 3) Port_2 = atoi(argv[2]);

    struct sockaddr_in user1addr, user2addr;
    user1addr.sin_family = AF_INET; 
    user1addr.sin_addr.s_addr = inet_addr(IP_1); 
    user1addr.sin_port = htons(Port_1); 
    user2addr.sin_family = AF_INET; 
    user2addr.sin_addr.s_addr = inet_addr(IP_2); 
    user2addr.sin_port = htons(Port_2); 

    // bind ksocket
    k_bind(M1, &user1addr, &user2addr);
    
    // send file contents to user2
    int fd = open("dummy.txt", O_RDONLY);
    char buff[BLOCKSIZE];
    bzero(buff, sizeof(buff));
    // read and send till EOF
    while(read(fd, buff, sizeof(buff)) != 0) {
        while(k_sendto(M1, buff, sizeof(buff), 0, &user2addr, sizeof(user2addr))<0) sleep(10);
        bzero(buff, sizeof(buff));
    }
    close(fd);
    // send EOF marker
    buff[0] = '%';
    while(k_sendto(M1, buff, sizeof(buff), 0, &user2addr, sizeof(user2addr))<0) sleep(10);
    
    // receive end confirmation
    while(k_recvfrom(M1, buff, sizeof(buff), 0, NULL, NULL) < 0);

    // close ksocket
    k_close(M1);

    return 0;
}