#ifndef DMA_H
#define DMA_H

//#define LVZERO 1

#ifndef RH850EMUL_H
#define CACHE_MISS_LATENCY 50
#define CACHE_BLOCK_SIZE 16
#define WORD_SIZE 4
#else
#define CACHE_MISS_LATENCY 10
#define CACHE_BLOCK_SIZE 16 
#define WORD_SIZE 16 
#endif

int Cdma(int functionID);
int CdmaByBytes(int nBytes);

#endif
