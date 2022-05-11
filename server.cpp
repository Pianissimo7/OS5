/*
** server.c -- a stream socket server demo
*/

#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <sys/wait.h>
#include <signal.h>
#include <pthread.h>
#include "stack.hpp"
#include <iostream>       // std::cout
#include <mutex>          // std::mutex
#include <sys/mman.h>   // mmap
#include <sys/stat.h>
#include <fcntl.h>
#include <semaphore.h>

#define PORT "3500"  // the port users will be connecting to

#define BACKLOG 10   // how many pending connections queue will hold

struct arg_struct {
    int arg1;
    pnode *head;
    int ** size;
};


void push(pnode *head, char data[1024]); // push -> receives head of stack (double pointer) & data array
void pop(pnode *head);  // pop -> receives head of stack (double pointer)
char* top(pnode head);  // top -> receives head of stack (pointer)
void* _malloc(size_t size);
void* _calloc(size_t size);
void _free(void *address);

static pthread_mutex_t lock;        // mutex lock -> QUES 6

void sigchld_handler(int s)
{
    // waitpid() might overwrite errno, so we save and restore it:
    int saved_errno = errno;

    while(waitpid(-1, NULL, WNOHANG) > 0);

    errno = saved_errno;
}


// get sockaddr, IPv4 or IPv6:
void *get_in_addr(struct sockaddr *sa)
{
    if (sa->sa_family == AF_INET) {
        return &(((struct sockaddr_in*)sa)->sin_addr);
    }

    return &(((struct sockaddr_in6*)sa)->sin6_addr);
}

void *send_to_user(pnode * head, int ** size, int new_fd)
{
    
    sleep(2);
    char buffer[1024] = {0};
    while(true){
        read(new_fd, buffer, 1024);
        if (strncmp(buffer, "PUSH ",5) == 0)
        {
            // the PUSH command is executed
            pthread_mutex_lock(&lock);
            strcpy(head[**size]->data, buffer+5);
            (**size) = (**size) + 1;
            pthread_mutex_unlock(&lock);
        }
        else if (strncmp(buffer, "TOP",3) == 0)
        {
            // the POP command is executed
            pthread_mutex_lock(&lock);
            char* str = head[**size-1]->data;
            if(**size == 0){
                char emp[2] = {'-', '\0'};
                send(new_fd, emp, strlen(emp),0);
            }
            else if(send(new_fd, str, strlen(str),0) == -1){
                perror("send error!");
            }
            pthread_mutex_unlock(&lock);
        }
        else if (strncmp(buffer, "POP",3) == 0)
        {
            // the POP command is executed
            pthread_mutex_lock(&lock);
            strcpy(head[**size-1]->data, "");
            (**size) = (**size) - 1;
            pthread_mutex_unlock(&lock);
        }
        else if (strncmp(buffer, "EXIT",4) == 0){
            printf("Connection stopped from one client\n");
            break;
        }
        bzero(buffer, 1024);
    }
    
    close(new_fd);
    return NULL;
}

int main(void)
{
    int sockfd, new_fd;  // listen on sock_fd, new connection on new_fd
    struct addrinfo hints, *servinfo, *p;
    struct sockaddr_storage their_addr; // connector's address information
    socklen_t sin_size;
    struct sigaction sa;
    int yes=1;
    char s[INET6_ADDRSTRLEN];
    int rv;
    pnode head;
    int *size;

    // stack using mmap
    head = (pnode) mmap(
        NULL,
        sizeof(node)*1000,
        PROT_READ | PROT_WRITE,
        MAP_SHARED | MAP_ANONYMOUS,
        -1,
        0
    );
    
    size = (int*) mmap(
        NULL,
        sizeof(int),
        PROT_READ | PROT_WRITE,
        MAP_SHARED | MAP_ANONYMOUS,
        -1,
        0
    );
    *size = 0;      // size of the current stack, will use as a pointer to the shared memory

    strcpy(head->data, "EMPTY");
    head->next = head + sizeof(node);

    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE; // use my IP

    if ((rv = getaddrinfo(NULL, PORT, &hints, &servinfo)) != 0) {
        fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(rv));
        return 1;
    }

    // loop through all the results and bind to the first we can
    for(p = servinfo; p != NULL; p = p->ai_next) {
        if ((sockfd = socket(p->ai_family, p->ai_socktype,
                p->ai_protocol)) == -1) {
            perror("server: socket");
            continue;
        }

        if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &yes,
                sizeof(int)) == -1) {
            perror("setsockopt");
            exit(1);
        }

        if (bind(sockfd, p->ai_addr, p->ai_addrlen) == -1) {
            close(sockfd);
            perror("server: bind");
            continue;
        }

        break;
    }

    freeaddrinfo(servinfo); // all done with this structure

    if (p == NULL)  {
        fprintf(stderr, "server: failed to bind\n");
        exit(1);
    }

    if (listen(sockfd, BACKLOG) == -1) {
        perror("listen");
        exit(1);
    }

    sa.sa_handler = sigchld_handler; // reap all dead processes
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART;
    if (sigaction(SIGCHLD, &sa, NULL) == -1) {
        perror("sigaction");
        exit(1);
    }

    printf("server: waiting for connections...\n");

    while(1) {  // main accept() loop
        sin_size = sizeof their_addr;
        new_fd = accept(sockfd, (struct sockaddr *)&their_addr, &sin_size);
        if (new_fd == -1) {
            perror("accept");
            continue;
        }

        inet_ntop(their_addr.ss_family,
            get_in_addr((struct sockaddr *)&their_addr),
            s, sizeof s);
        printf("server: got connection from %s\n", s);

        pid_t c_pid = fork();

        if (c_pid == -1) {
            perror("fork");
            exit(EXIT_FAILURE);
        } else if (c_pid == 0) {
            send_to_user(&head, &size, new_fd);
        }
        close(new_fd);
    }

    return 0;
}