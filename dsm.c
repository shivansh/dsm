// A distributed shared memory system
//
// The following implementation models a two process system as a proof of
// concept for a n-process system.

#include <arpa/inet.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#include "dsm.h"

#define pageSize sysconf(_SC_PAGESIZE)

// The virtual address 2^30 is taken as the base address of the shared virtual
// memory segment. The eerie address hardcoding allows both the processes to
// have the same view of the virtual memory. TODO: figure out an alternative.
char *baseAddr = (char *)(1 << 30);

int localSock, remoteSock;

// To avoid race conditions, we maintain a per page mutex.
pthread_mutex_t *pageMutex;

// pageFaultHandler is invoked when a page fault is encountered. It is
// responsible for fetching the appropriate page from the remote machine.
void pageFaultHandler(int sig, siginfo_t *info, void *ucontext) {
    int offset = 0;
    int bytesReceived = 0;
    int bytesPending = pageSize;
    char sendBuf[32];                 // buffer to store page number
    char recvBuf[pageSize];           // buffer to store incoming page
    char *faultAddr = info->si_addr;  // address of the faulting page
    int pageNumber = ((int)(faultAddr - baseAddr)) / pageSize;

    // Lock the current page using per page mutex locking.
    pthread_mutex_lock(&pageMutex[pageNumber]);

    sprintf(sendBuf, "%d", pageNumber);
    send(remoteSock, sendBuf, strlen(sendBuf), 0);
    while (bytesPending > 0) {
        bytesReceived = recv(remoteSock, &recvBuf[offset], bytesPending, 0);
        bytesPending -= bytesReceived;
        offset += bytesReceived;
    }

    // Mark the received page as write-only.
    char *startAddr = baseAddr + (pageNumber * pageSize);
    if (mprotect(startAddr, pageSize, PROT_WRITE)) {
        perror("mprotect");
        exit(1);
    }

    // Copy the received page into the local memory.
    memcpy(startAddr, recvBuf, pageSize);

    // Unlock the mutex.
    pthread_mutex_unlock(&pageMutex[pageNumber]);
}

// pageServer listens for incoming page requests and serves the relevant pages.
void *pageServer(void *ptr) {
    int sockfd = (int *)ptr;
    int bytesReceived;
    char recvBuf[32];  // buffer to store incoming page number
    while (1) {
        bytesReceived = recv(sockfd, recvBuf, 32, 0);
        recvBuf[bytesReceived] = '\0';
        int pageNumber = atoi(recvBuf);

        // Lock the current page using per page mutex locking.
        pthread_mutex_lock(&pageMutex[pageNumber]);

        // The process is allowed to read the current page while is being
        // copied. However, writes to this page should not be allowed in this
        // duration, thus it should be marked as read-only.
        char *startAddr = baseAddr + pageNumber * pageSize;
        if (mprotect(startAddr, pageSize, PROT_READ)) {
            perror("mprotect");
            exit(1);
        }
        send(sockfd, startAddr, pageSize, 0);

        // After the page has been sent to the other process, the current
        // process has to relinquish its control over this page.
        if (mprotect(startAddr, pageSize, PROT_NONE)) {
            perror("mprotect");
            exit(1);
        }

        // Unlock the mutex.
        pthread_mutex_unlock(&pageMutex[pageNumber]);
    }
}

// initSharedMemory initializes the initial state of the distributed shared
// memory amongst the master and slave processes.
void initSharedMemory(int isMaster, int numPages, char *masterIP,
                      int masterPort, char *slaveIP, int slavePort) {
    // Setup signal handler to close the sockets on SIGINT.
    signal(SIGINT, atExit);

    // Setup signal handler for page faults.
    struct sigaction sa;
    sa.sa_sigaction = pageFaultHandler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_SIGINFO;
    if (sigaction(SIGSEGV, &sa, 0)) {
        perror("sigaction");
        exit(1);
    }

    // Initially, the slave owns the first half of the shared address space and
    // the master owns the second half.
    char *startAddr =
        mmap((void *)(1 << 30), numPages * pageSize, PROT_READ | PROT_WRITE,
             MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (!isMaster) {
        startAddr += pageSize * (numPages / 2);
    }
    if (mprotect(startAddr, pageSize * (numPages / 2), PROT_NONE)) {
        perror("mprotect");
        exit(1);
    }

    // Setup TCP/IP sockets.
    struct sockaddr_in localAddr, remoteAddr, clientAddr;
    char *localIP = malloc(64);
    char *remoteIP = malloc(64);
    int len = sizeof(struct sockaddr_in);
    int sockfd;

    if (isMaster) {
        // master process
        strcpy(localIP, masterIP);
        strcpy(remoteIP, slaveIP);
        localAddr.sin_port = htons(masterPort);
        remoteAddr.sin_port = htons(slavePort);
    } else {
        // slave process
        strcpy(localIP, slaveIP);
        strcpy(remoteIP, masterIP);
        localAddr.sin_port = htons(slavePort);
        remoteAddr.sin_port = htons(masterPort);
    }

    localSock = socket(AF_INET, SOCK_STREAM, 0);
    setsockopt(localSock, SOL_SOCKET, SO_REUSEADDR, NULL, sizeof(int));
    localAddr.sin_family = AF_INET;
    inet_pton(AF_INET, localIP, &localAddr.sin_addr);

    remoteSock = socket(AF_INET, SOCK_STREAM, 0);
    setsockopt(remoteSock, SOL_SOCKET, SO_REUSEADDR, NULL, sizeof(int));
    remoteAddr.sin_family = AF_INET;
    inet_pton(AF_INET, remoteIP, &remoteAddr.sin_addr);

    if (isMaster) {
        bind(localSock, (struct sockaddr *)&localAddr, sizeof(struct sockaddr));
        listen(localSock, 5);
        sockfd = accept(localSock, (struct sockaddr *)&clientAddr, &len);
        sleep(2);  // wait for slave process to start
        connect(remoteSock, (struct sockaddr *)&remoteAddr,
                sizeof(struct sockaddr));
    } else {
        connect(remoteSock, (struct sockaddr *)&remoteAddr,
                sizeof(struct sockaddr));
        bind(localSock, (struct sockaddr *)&localAddr, sizeof(struct sockaddr));
        listen(localSock, 5);
        sockfd = accept(localSock, (struct sockaddr *)&clientAddr, &len);
    }

    // Initialize per page mutexes.
    pageMutex = malloc(sizeof(pthread_mutex_t) * numPages);

    pthread_t serverThread;
    pthread_create(&serverThread, NULL, pageServer, (void *)sockfd);
}

// atExit closes the sockets when SIGINT is encountered.
void atExit() {
    close(remoteSock);
    close(localSock);
    exit(0);
}

// getBaseAddress returns the base address of the shared memory region.
void *getBaseAddress() { return (void *)(1 << 30); }
