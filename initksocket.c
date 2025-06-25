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
#include <sys/shm.h>
#include <sys/select.h>
#include <sys/syscall.h>
#include <signal.h>
#include <unistd.h>
#include "ksocket.h"

int shmid;
ksock* SM;

// signal handler function to remove shared memory on interrupt
void sighandler() {
    shmdt(SM);
    shmctl(shmid, IPC_RMID, 0);
    exit(0);
}

// helper function to get message index from sequence number
int getindexfromseq(int seq, int low, int len) {
    int high = (low + len - 1) % SEQLIM;
    if(low <= high) {
        if(low <= seq && seq <= high) 
            return seq - low + 1;
        else 
            return -1;
    }
    if(low <= seq)
        return seq - low + 1;
    if(seq <= high)
        return len - high + seq;
    return -1;
}

void garbage_collector(int sockid);
void* R(void* args);
void* S(void* args);

int main() {
    srand(time(NULL));
    // create shared memory for N ksockets
    shmid = shmget(ftok("/", 'A'), N * sizeof(ksock), IPC_CREAT | 0666);
    if(shmid == -1) {
        printf("Error creating shared memory\n");
        exit(1);
    }

    SM = (ksock*) shmat(shmid, NULL, 0);

    // set up mutex locks to be shared amongst processes
    pthread_mutexattr_t attr;
    pthread_mutexattr_init(&attr);
    pthread_mutexattr_setpshared(&attr, PTHREAD_PROCESS_SHARED);
    for(int i=0; i<N; i++) {
        pthread_mutex_init(&SM[i].mutex, &attr);
        SM[i].status = 0;    // mark sockets as free
    }
    
    // handle SIGINT to delete shared memory
    signal(SIGINT, sighandler); 

    // create threads
    pthread_t Rid, Sid;
    pthread_create(&Rid, NULL, R, NULL);
    pthread_create(&Sid, NULL, S, NULL);
    pthread_join(Rid, NULL);
    pthread_join(Sid, NULL);

    return 0;
}

// function to close unclosed ksockets
void garbage_collector(int sockid) {
    SM[sockid].status = 0;
    SM[sockid].PID = 0;
    bzero(&SM[sockid].rec_addr, sizeof(struct sockaddr_in));
    close(SM[sockid].UDP_sockfd);
}

// thread for receiving messages
void* R(void* args) {
    fd_set rfds;
    struct timeval tv;

    while(1) {
        tv.tv_sec = T/2;
        tv.tv_usec = 0;
        FD_ZERO(&rfds);
        int UDP_fd[N];
        int maxfd = -1;

        // add active UDP socket fds to read fd set
        for(int i=0; i<N; i++) {
            UDP_fd[i] = -1;
            pthread_mutex_lock(&SM[i].mutex);

            if(SM[i].status == 1) {
                int pidfd = syscall(SYS_pidfd_open, SM[i].PID, 0);
                if(pidfd < 0) {
                    garbage_collector(i);
                }
                else {
                    UDP_fd[i] = syscall(SYS_pidfd_getfd, pidfd, SM[i].UDP_sockfd, 0);
                    FD_SET(UDP_fd[i], &rfds);
                    if(UDP_fd[i] > maxfd) maxfd = UDP_fd[i];
                    close(pidfd);
                }
            }

            pthread_mutex_unlock(&SM[i].mutex);
        }
        if(maxfd == -1) continue;   // no socket active

        // select from UDP fds
        int nready = select(maxfd + 1, &rfds, NULL, NULL, &tv);
        if(nready > 0) {
            for(int i=0; i<N; i++) {
                // receive UDP datagram
                if(UDP_fd[i] != -1 && FD_ISSET(UDP_fd[i], &rfds)) {
                    char temp_buff[8 + BLOCKSIZE];
                    recvfrom(UDP_fd[i], temp_buff, sizeof(temp_buff), 0, NULL, NULL);
                    if(dropMessage()) continue;    // drop message based on probability
                    
                    // extract message header
                    char header[4];
                    memcpy(header, temp_buff, 3);
                    header[3] = '\0';

                    // nospace query
                    if(strcmp(header, "NSP") == 0) {
                        pthread_mutex_lock(&SM[i].mutex);
                        if(SM[i].rwnd.wnd_size > 0) {   // if space is available now
                            SM[i].rwnd.nospace = 0;
                            // send duplicate acknowledgement with window size
                            char buff[10];
                            int prev_seq = (SM[i].rwnd.start_seq - 1 + SEQLIM) % SEQLIM;
                            sprintf(buff, "ACK %02X %X", prev_seq, SM[i].rwnd.wnd_size);
                            sendto(UDP_fd[i], buff, sizeof(buff), 0, (struct sockaddr*) &SM[i].rec_addr, sizeof(struct sockaddr_in));
                        }    
                        pthread_mutex_unlock(&SM[i].mutex);
                    }

                    // acknowledgement
                    else if(strcmp(header, "ACK") == 0) {
                        int seq_num = (int) strtol(temp_buff + 4, NULL, 16);
                        int wsize = (int) strtol(temp_buff + 7, NULL, 16);
                        pthread_mutex_lock(&SM[i].mutex);
                        if((seq_num + 1) % SEQLIM == SM[i].swnd.start_seq) {    // duplicate for window resize
                            if(SM[i].swnd.wnd_size < wsize)     // update window size
                                SM[i].swnd.wnd_size = wsize;
                        }

                        else {
                            int index = getindexfromseq(seq_num, SM[i].swnd.start_seq, SM[i].send_buff.msg_cnt - SM[i].swnd.unsent);
                            if(index > 0) {    // non-duplicate
                                // update send window
                                SM[i].send_buff.msg_cnt -= index;
                                SM[i].send_buff.start = (SM[i].send_buff.start + index) % BUFFSIZE;
                                SM[i].swnd.start_seq = (SM[i].swnd.start_seq + index) % SEQLIM;
                                SM[i].swnd.wnd_size = wsize;
                            }
                        }
                        pthread_mutex_unlock(&SM[i].mutex);
                    }

                    // message
                    else {
                        int seq_num = (int) strtol(temp_buff + 4, NULL, 16);
                        pthread_mutex_lock(&SM[i].mutex);
                        int index = getindexfromseq(seq_num, SM[i].rwnd.start_seq, SM[i].rwnd.wnd_size);
                        if(index > 0) {    // non-duplicate
                            // receive message
                            index = (SM[i].rec_buff.start + index - 1) % BUFFSIZE;
                            SM[i].received[index] = 1;
                            memcpy(SM[i].rec_buff.arr[index], temp_buff + 8, BLOCKSIZE);
                            // update send window
                            int cnt = 0;
                            while(SM[i].rwnd.wnd_size > 0 && SM[i].received[SM[i].rec_buff.start]) {
                                SM[i].rwnd.wnd_size--;
                                SM[i].rec_buff.msg_cnt++;
                                SM[i].rec_buff.start = (SM[i].rec_buff.start + 1) % BUFFSIZE;
                                SM[i].rwnd.start_seq = (SM[i].rwnd.start_seq + 1) % SEQLIM;
                                cnt++;
                            }
                            if(cnt > 0) {
                                // send acknowledgement
                                char buff[10];
                                int prev_seq = (SM[i].rwnd.start_seq - 1 + SEQLIM) % SEQLIM;
                                sprintf(buff, "ACK %02X %X", prev_seq, SM[i].rwnd.wnd_size);
                                sendto(UDP_fd[i], buff, sizeof(buff), 0, (struct sockaddr*) &SM[i].rec_addr, sizeof(struct sockaddr_in));
                            }
                            if(SM[i].rwnd.wnd_size == 0)
                                SM[i].rwnd.nospace = 1;
                        }

                        else {     // duplicate
                            // send duplicate acknowledgement
                            char buff[10];
                            int prev_seq = (SM[i].rwnd.start_seq - 1 + SEQLIM) % SEQLIM;
                            sprintf(buff, "ACK %02X %X", prev_seq, SM[i].rwnd.wnd_size);
                            sendto(UDP_fd[i], buff, sizeof(buff), 0, (struct sockaddr*) &SM[i].rec_addr, sizeof(struct sockaddr_in));
                        }

                        pthread_mutex_unlock(&SM[i].mutex);
                    }
                }
            }
        }
        
        else {  // timeout
            for(int i=0; i<N; i++) {
                pthread_mutex_lock(&SM[i].mutex);
                // check for nospace
                if(UDP_fd[i] != -1 && SM[i].rwnd.nospace && SM[i].rwnd.wnd_size > 0) {
                    SM[i].rwnd.nospace = 0;
                    // send duplicate acknowledgement
                    char buff[10];
                    int prev_seq = (SM[i].rwnd.start_seq - 1 + SEQLIM) % SEQLIM;
                    sprintf(buff, "ACK %02X %X", prev_seq, SM[i].rwnd.wnd_size);
                    sendto(UDP_fd[i], buff, sizeof(buff), 0, (struct sockaddr*) &SM[i].rec_addr, sizeof(struct sockaddr_in));
                }    
                pthread_mutex_unlock(&SM[i].mutex);
            }
        }

        // close the UDP fds
        for(int i=0; i<N; i++) {
            if(UDP_fd[i] > 0) {
                close(UDP_fd[i]);
            }
        }
    }

    return NULL;
}

// thread for sending messages
void* S(void* args) {
    while(1) {
        sleep(T/2);
        for(int i=0; i<N; i++) {
            pthread_mutex_lock(&SM[i].mutex);
            if(SM[i].status == 1 && (SM[i].send_buff.msg_cnt > 0 || SM[i].swnd.wnd_size == 0)) {
                if(difftime(time(NULL), SM[i].last_sent) > T) {     // timeout
                    int pidfd = syscall(SYS_pidfd_open, SM[i].PID, 0);
                    if(pidfd < 0) {
                        garbage_collector(i);
                    }
                    else {
                        int UDP_fd = syscall(SYS_pidfd_getfd, pidfd, SM[i].UDP_sockfd, 0);
                        close(pidfd);
                        if(SM[i].swnd.wnd_size == 0) {
                            // send nospace query
                            char buff[10];
                            sprintf(buff, "NSP");
                            sendto(UDP_fd, buff, sizeof(buff), 0, (struct sockaddr*) &SM[i].rec_addr, sizeof(struct sockaddr_in));
                        }

                        else {
                            // resend all messages in window
                            int index = SM[i].send_buff.start;
                            int seq_num = SM[i].swnd.start_seq;
                            char temp_buff[8 + BLOCKSIZE];
                            for(int j=0; j<SM[i].send_buff.msg_cnt; j++) {
                                sprintf(temp_buff, "MSG %02X", seq_num);
                                memcpy(temp_buff + 8, SM[i].send_buff.arr[index], BLOCKSIZE);
                                sendto(UDP_fd, temp_buff, sizeof(temp_buff), 0, (struct sockaddr*) &SM[i].rec_addr, sizeof(struct sockaddr_in));
                                index = (index + 1) % BUFFSIZE;
                                seq_num = (seq_num + 1) % SEQLIM;
                            }
                            SM[i].swnd.unsent = 0;
                            SM[i].last_sent = time(NULL);
                        }
                        close(UDP_fd);
                    }
                }

                else if(SM[i].swnd.unsent > 0) {    // send unsent messages
                    int pidfd = syscall(SYS_pidfd_open, SM[i].PID, 0);
                    if(pidfd < 0) {
                        garbage_collector(i);
                    }
                    else {
                        int UDP_fd = syscall(SYS_pidfd_getfd, pidfd, SM[i].UDP_sockfd, 0);
                        close(pidfd);
                        int index = (SM[i].send_buff.start + SM[i].send_buff.msg_cnt - SM[i].swnd.unsent) % BUFFSIZE;
                        int seq_num = (SM[i].swnd.start_seq + SM[i].send_buff.msg_cnt - SM[i].swnd.unsent) % SEQLIM;
                        char temp_buff[8 + BLOCKSIZE];
                        while(SM[i].swnd.unsent > 0) {
                            sprintf(temp_buff, "MSG %02X", seq_num);
                            memcpy(temp_buff + 8, SM[i].send_buff.arr[index], BLOCKSIZE);
                            sendto(UDP_fd, temp_buff, sizeof(temp_buff), 0, (struct sockaddr*) &SM[i].rec_addr, sizeof(struct sockaddr_in));
                            index = (index + 1) % BUFFSIZE;
                            seq_num = (seq_num + 1) % SEQLIM;
                            SM[i].swnd.unsent--;
                        }
                        SM[i].last_sent = time(NULL);
                        close(UDP_fd);
                    }
                }
            }
            pthread_mutex_unlock(&SM[i].mutex);
        }
    }
    return NULL;
}