/*
=====================================
Assignment 4 Submission
Name: Dev Butani
Roll number: 22CS30022
=====================================
*/

#include <stdlib.h>
#include <string.h>
#include "ksocket.h"

int ERROR = 0;  // global error variable

// function to create a ksocket
int k_socket(int domain, int type, int protocol) {
    if(type != SOCK_KTP || protocol != 0) {
        ERROR = EINVALID;
        return -1;
    }

    // find a free socket
    int shmid = shmget(ftok("/", 'A'), 0, 0);
    if(shmid == -1) {
        ERROR = ESCKTFAIL;
        return -1;
    }
    ksock* SM = (ksock*) shmat(shmid, NULL, 0);
    int i;
    for(i=0; i<N; i++) {
        pthread_mutex_lock(&SM[i].mutex);
        if(SM[i].status == 0) {
            break;
        }
        pthread_mutex_unlock(&SM[i].mutex);
    }
    if(i == N) {
        ERROR = ENOSPACE;
        shmdt(SM);
        return -1;
    }
    
    // create corresponding UDP socket
    SM[i].UDP_sockfd = socket(domain, SOCK_DGRAM, 0);
    if(SM[i].UDP_sockfd < 0) {
        ERROR = EINVALID;
        pthread_mutex_unlock(&SM[i].mutex);
        shmdt(SM);
        return -1;
    }
    bzero(&SM[i].rec_addr, sizeof(struct sockaddr_in));
    
    // initialize fields
    SM[i].status = 1;
    SM[i].PID = getpid();

    SM[i].send_buff.start = 0;
    SM[i].send_buff.end = 0;
    SM[i].send_buff.msg_cnt = 0;

    SM[i].swnd.wnd_size = 10;
    SM[i].swnd.start_seq = 1;
    SM[i].swnd.unsent = 0;

    SM[i].rec_buff.start = 0;
    SM[i].rec_buff.end = 0;
    SM[i].rec_buff.msg_cnt = 0;

    SM[i].rwnd.wnd_size = 10;
    SM[i].rwnd.start_seq = 1;
    SM[i].rwnd.nospace = 0;

    for(int i=0; i<BUFFSIZE; i++) {
        SM[i].received[i] = 0;
    }

    SM[i].last_sent = time(NULL);

    pthread_mutex_unlock(&SM[i].mutex);
    shmdt(SM);
    return i;
}

// function to bind ksocket
int k_bind(int ksockfd, const struct sockaddr_in *source_addr, const struct sockaddr_in *dest_addr) {
    int shmid = shmget(ftok("/", 'A'), 0, 0);
    if(shmid == -1 || ksockfd < 0 || ksockfd >= N) {
        ERROR = ESCKTFAIL;
        return -1;
    }

    ksock* SM = (ksock*) shmat(shmid, NULL, 0);
    pthread_mutex_lock(&SM[ksockfd].mutex);
    // bind corresponding UDP socket
    int b = bind(SM[ksockfd].UDP_sockfd, (struct sockaddr*) source_addr, sizeof(struct sockaddr_in));
    if(b < 0) {
        ERROR = EINVALID;
        pthread_mutex_unlock(&SM[ksockfd].mutex);
        shmdt(SM);
        return -1;
    } 

    // set receivers address
    SM[ksockfd].rec_addr.sin_family = dest_addr->sin_family;
    SM[ksockfd].rec_addr.sin_addr.s_addr = dest_addr->sin_addr.s_addr;
    SM[ksockfd].rec_addr.sin_port = dest_addr->sin_port;
    
    pthread_mutex_unlock(&SM[ksockfd].mutex);
    shmdt(SM);
    return 0;
}

// function to send messages
ssize_t k_sendto(int ksockfd, const void *buff, size_t len, int flags, const struct sockaddr_in *dest_addr, socklen_t addrlen) {
    int shmid = shmget(ftok("/", 'A'), 0, 0);
    if(shmid == -1 || ksockfd < 0 || ksockfd >= N) {
        ERROR = ESCKTFAIL;
        return -1;
    }

    ksock* SM = (ksock*) shmat(shmid, NULL, 0);
    pthread_mutex_lock(&SM[ksockfd].mutex);
    // validate destination address as bound receivers address
    if(SM[ksockfd].rec_addr.sin_addr.s_addr != dest_addr->sin_addr.s_addr || SM[ksockfd].rec_addr.sin_port != dest_addr->sin_port) {
        ERROR = ENOTBOUND;
        pthread_mutex_unlock(&SM[ksockfd].mutex);
        shmdt(SM);
        return -1;
    }

    // check for space in window
    if(SM[ksockfd].send_buff.msg_cnt >= SM[ksockfd].swnd.wnd_size) {
        ERROR = ENOSPACE;
        pthread_mutex_unlock(&SM[ksockfd].mutex);
        shmdt(SM);
        return -1;
    }

    // transfer message to internal buffer
    if(len > BLOCKSIZE) len = BLOCKSIZE;
    SM[ksockfd].send_buff.msg_cnt++;
    bzero(SM[ksockfd].send_buff.arr[SM[ksockfd].send_buff.end], BLOCKSIZE);
    memcpy(SM[ksockfd].send_buff.arr[SM[ksockfd].send_buff.end++], buff, len);
    SM[ksockfd].send_buff.end %= BUFFSIZE;
    SM[ksockfd].swnd.unsent++;

    pthread_mutex_unlock(&SM[ksockfd].mutex);
    shmdt(SM);
    return len;
}

// function to receive messages
ssize_t k_recvfrom(int ksockfd, void *buff, size_t len, int flags, struct sockaddr_in *src_addr, socklen_t *addrlen) {
    int shmid = shmget(ftok("/", 'A'), 0, 0);
    if(shmid == -1 || ksockfd < 0 || ksockfd >= N) {
        ERROR = ESCKTFAIL;
        return -1;
    }

    ksock* SM = (ksock*) shmat(shmid, NULL, 0);
    pthread_mutex_lock(&SM[ksockfd].mutex);
    // check if messages are present in buffer
    if(SM[ksockfd].rec_buff.msg_cnt == 0) {
        ERROR = ENOMESSAGE;
        pthread_mutex_unlock(&SM[ksockfd].mutex);
        shmdt(SM);
        return -1;
    }

    // transfer message to provided buffer
    if(len > BLOCKSIZE) len = BLOCKSIZE;
    SM[ksockfd].received[SM[ksockfd].rec_buff.end] = 0;
    memcpy(buff, SM[ksockfd].rec_buff.arr[SM[ksockfd].rec_buff.end++], len);
    SM[ksockfd].rec_buff.end %= BUFFSIZE;
    SM[ksockfd].rec_buff.msg_cnt--;
    SM[ksockfd].rwnd.wnd_size++;

    // set source address if required
    if(src_addr != NULL) {
        src_addr->sin_family = AF_INET;
        src_addr->sin_addr.s_addr = SM[ksockfd].rec_addr.sin_addr.s_addr;
        src_addr->sin_port = SM[ksockfd].rec_addr.sin_port;
        if(addrlen) *addrlen = sizeof(struct sockaddr_in);
    }

    pthread_mutex_unlock(&SM[ksockfd].mutex);
    shmdt(SM);
    return len;
}

// function to close ksocket
int k_close(int ksockfd) {
    int shmid = shmget(ftok("/", 'A'), 0, 0);
    if(shmid == -1 || ksockfd < 0 || ksockfd >= N) {
        ERROR = ESCKTFAIL;
        return -1;
    }

    ksock* SM = (ksock*) shmat(shmid, NULL, 0);
    pthread_mutex_lock(&SM[ksockfd].mutex);
    
    // reset fields and close corresponding UDP socket
    SM[ksockfd].status = 0;
    SM[ksockfd].PID = 0;
    bzero(&SM[ksockfd].rec_addr, sizeof(struct sockaddr_in));
    int c = close(SM[ksockfd].UDP_sockfd);

    pthread_mutex_unlock(&SM[ksockfd].mutex);
    shmdt(SM);
    
    if(c < 0) {
        ERROR = EINVALID;
        return -1;
    }
    
    return 0;
}

// function to drop messages
int dropMessage() {
    float r = rand() / (float) RAND_MAX;
    return r < p; 
}