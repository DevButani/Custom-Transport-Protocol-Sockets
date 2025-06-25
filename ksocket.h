/*
=====================================
Assignment 4 Submission
Name: Dev Butani
Roll number: 22CS30022
=====================================
*/

#include <sys/socket.h> 
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/shm.h>
#include <unistd.h> 
#include <pthread.h>
#include <time.h>

#define N 20            // total number of ksockets
#define T 5             // message timeout period
#define p 0.05          // message drop probability
#define BLOCKSIZE 512   // message block size
#define BUFFSIZE 10     // size of send and receive buffers
#define SEQLIM 256      // sequence number limit (8 bit seq no.s)
#define SOCK_KTP 69     // KTP Socket type
#define ESCKTFAIL 5     // failed to access sockets
#define EINVALID 6      // invalid arguments
#define ENOSPACE 7      // no free space error
#define ENOTBOUND 8     // destination IP/port not bound error
#define ENOMESSAGE 9    // no message available 
extern int ERROR;       // global error variable

// structure for buffer
typedef struct {
    int start;                      // start message index 
    int end;                        // end message index
    int msg_cnt;                    // number of messages in buffer
    char arr[BUFFSIZE][BLOCKSIZE];  // array storing messages
} buff;

// structure for window
typedef struct {
    int wnd_size;     // window size
    int start_seq;    // starting sequence number
    union {
        int unsent;   // count of unsent messages
        int nospace;  // nospace flag
    };
} window;

// structire for ksocket
typedef struct {
    pthread_mutex_t mutex;        // mutex lock
    int status;                   // 0 for free, 1 for alloted
    int PID;                      // PID of linked process
    int UDP_sockfd;               // file descriptor of corresponding UDP socket
    struct sockaddr_in rec_addr;  // socket address of receiver
    buff send_buff;               // send buffer
    window swnd;                  // sender window
    buff rec_buff;                // receive buffer
    window rwnd;                  // receiver window
    int received[BUFFSIZE];       // marks filled buffer indices
    time_t last_sent;             // timestamp of last message-send
} ksock;

// function to create a ksocket
int k_socket(int domain, int type, int protocol);

// function to bind ksocket
int k_bind(int ksockfd, const struct sockaddr_in *source_addr, const struct sockaddr_in *dest_addr);

// function to send messages
ssize_t k_sendto(int ksockfd, const void *buff, size_t len, int flags, const struct sockaddr_in *dest_addr, socklen_t addrlen);

// function to receive messages
ssize_t k_recvfrom(int ksockfd, void *buff, size_t len, int flags, struct sockaddr_in *src_addr, socklen_t *addrlen);

// function to close ksocket
int k_close(int ksockfd);

// function to drop messages
int dropMessage();