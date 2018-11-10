#ifndef DSM_H_
#define DSM_H_

void initSharedMemory(int, int, char *, int, char *, int);
void atExit();
void *getBaseAddress();

#endif  // DSM_H_
