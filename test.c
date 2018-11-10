// This file implements test routines for testing the DSM implementation.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "dsm.h"

// The following test demonstrates the transfer of the first page of the DSM
// amongst master and slave processes.
int testA(int isMaster) {
    void *baseAddr = getBaseAddress();

    if (isMaster) {
        int *ptr = (int *)baseAddr;
        int i = 0;
        while (1) {
            *ptr = i;
            usleep(500000);
            printf("master: %d\n", *ptr);
            ++i;
        }
    } else {
        while (1) {
            int *ptr = (int *)baseAddr;
            *ptr += 4;
            usleep(500000);
            printf("slave: %d\n", *ptr);
        }
    }
}

int main(int argc, char **argv) {
    int isMaster = !strcmp(argv[1], "master");
    int numPages = atoi(argv[2]);
    char *masterIP = argv[3];
    int masterPort = atoi(argv[4]);
    char *slaveIP = argv[5];
    int slavePort = atoi(argv[6]);

    initSharedMemory(isMaster, numPages, masterIP, masterPort, slaveIP,
                     slavePort);

    testA(isMaster);

    atExit();
    return 0;  // not reached
}
